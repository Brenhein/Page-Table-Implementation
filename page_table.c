/**
* Brenden Hein
*
* page_table.c
*/

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <string.h>
#include <map>
#include <utility>
#include <iomanip>

using namespace std;

/**
* STRUCTS
*/

/*Parsed memory file line*/
struct MemFileLine
{
    unsigned int virtualAddress;
    string operation;
    unsigned int pageNumber;
    unsigned int offset;
    bool pageFault;
    bool writeBack;
    unsigned int frame;
};

/*Page Table Line*/
struct PageTableLine
{
    bool valid;
    bool referenced;
    bool modified;
    unsigned int frameNumber;
};

/*The page table configuration*/
struct Config
{
    string replacement;
    int numPages;
    vector<unsigned int> frames;
};

/*Global Variables*/
int PAGEFRAMES = 8192;
bool debug = false;
bool refs = false;
int PAGESIZE = 16777216 / PAGEFRAMES; // 2048 or 2^11
int OFFSET = 11;
int FRAMEBITS = 13;

string memFileName;
vector<MemFileLine> memFileLines;
map<unsigned int, PageTableLine> PageTable;
vector<unsigned int> freeFrameList;
struct Config configuration;

/*Memory reference counts*/
int refCnt = 0;
int readCnt = 0;
int writeCnt = 0;
int pageFaults = 0;
int writeBacks = 0;
int simParams = 0;

vector<int> pageQueue; ///< queue of the pages currently active
int head = 0; ///< head of the circular queue
int tail = 0; ///< tail of the circular queue


/*Processes the memory file*/
void process_memFile()
{
    ifstream memFile(memFileName);

    // The file failed to open
    if (memFile.fail())
    {
        cout << "Cannot open file '" << memFileName << "'" <<endl;
        exit(-1);
    }

    string line;

    // Reads through the lines of the file
    while (getline(memFile, line))
    {
        struct MemFileLine parsedLine; 

        // Parses for address
        string address = "";
        int i = 0;
        while (line[i] != ' ')
        {
            address += line[i];
            i++;
        }
        i++;

        // Stores the entire line as an unsingned int
        unsigned int hexLine = stoi(address, 0, 16);
        parsedLine.virtualAddress = hexLine;

        // Stores the operation
        parsedLine.operation = line[i];

        // Adds to read or write counter
        if (line[i] == 'R')
        {
            readCnt++;
        }
        else if (line[i] == 'W')
        {
            writeCnt++;
        }

        // Stores the page number
        unsigned int pageNum = (hexLine >> 11) & 0x7;
        parsedLine.pageNumber = pageNum;

        // Stores the offset
        unsigned int offset = hexLine & 0x7FF;
        parsedLine.offset = offset;

        // Adds the line to the memory lines
        memFileLines.push_back(parsedLine);

        // Adds 1 to reference count
        refCnt++;
    }
}


/*Initializes the page table to all records being zeroed out*/
void initialize_page_table()
{
    // Loops through the 16 lines of the page table
    for (unsigned int i = 0; i <= 0x7; i++)
    {
        struct PageTableLine line;

        // Sets the values of the line
        line.valid = false;
        line.referenced = false;
        line.modified = false;
        line.frameNumber = 0;

        // Sets the index
        PageTable[i] = line;
    }

}


/*Displays the page table*/
void display_page_table()
{
    cout << "\n";
    cout << "i V R M Frames" << endl;
    cout << "- - - - ------" << endl;

    // Loops through and displays the lines of the page table
    for (auto line : PageTable)
    {
        cout << hex << line.first << " ";
        cout << line.second.valid << " ";
        cout << line.second.referenced << " ";
        cout << line.second.modified << "  ";
        cout << hex << setfill('0') << setw(4) <<
            line.second.frameNumber << " ";
        cout << endl;
    }
}


/*Replaces a page using FIFO
* \returns The page to replace*/
unsigned int FIFO_replace()
{
    unsigned int pageToReplace = pageQueue[head];
    head = (head + 1) % configuration.numPages;
    return pageToReplace;
}


/*Replaces a page using clocking
* \returns The page to replace*/
unsigned int clock_replace(int i)
{
    while (PageTable[pageQueue[head]].referenced)
    {
        PageTable[pageQueue[head]].referenced = false;
        head = (head + 1) % configuration.numPages;
        tail = (tail + 1) % configuration.numPages;
    }
    int oldHead = head;
    head = (head + 1) % configuration.numPages;
    return pageQueue[oldHead];
}


/*Processes a line in the memory file
* \param index The index to the line in the memory reference container*/
void process_line(int index)
{
    // Initialize line data
    memFileLines[index].writeBack = false;
    memFileLines[index].pageFault = false;

    int pageNum = memFileLines[index].pageNumber;
    int frame = PageTable[pageNum].frameNumber;

    // Is the page in RAM?
    if (!PageTable[pageNum].valid) // Page is not valid (NOT IN RAM)
    {
        memFileLines[index].pageFault = true; // PAGE FAULT
        pageFaults++;

        // We got any free frames to pull from or do we have to kick one out?
        if (freeFrameList.size() == 0) // Page replacement
        {
            unsigned int pageToReplace; 

            // So what replacement algorithm are we using??
            if (configuration.replacement == "FIFO")
            {
                pageToReplace = FIFO_replace();
            }
            else if (configuration.replacement == "CLOCK")
            {
                pageToReplace = clock_replace(index);
            }

            // Places the victims page number in the free frame list and
            // removes it from the page table
            PageTable[pageToReplace].valid = false;
            unsigned int frame = PageTable[pageToReplace].frameNumber;
            freeFrameList.push_back(frame);

            // So we gotta write back??
            if (PageTable[pageToReplace].modified)
            {
                memFileLines[index].writeBack = true;
                writeBacks++;
            }
        }

        // Get the frame from the front of the free frame list
        frame = freeFrameList[0];
        freeFrameList.erase(freeFrameList.begin());

        // We got our frame now, so lets add that to the page table...
        PageTable[pageNum].frameNumber = frame;
        
        // Updates control bits
        PageTable[pageNum].valid = true; 
        PageTable[pageNum].referenced = false;
        PageTable[pageNum].modified = false;
        
        // Add page to the queue
        pageQueue[tail] = pageNum;
        tail = (tail + 1) % configuration.numPages;
    }

    // updates control bits and physical frame
    memFileLines[index].frame = frame;
    PageTable[pageNum].referenced = true;
    if (memFileLines[index].operation == "W")
    {
        PageTable[pageNum].modified = true;
    }
}


/*Processes the config file, if it exists*/
void process_config()
{
    ifstream configFile("config");

    // The file failed to open
    if (configFile.fail())
    {
        cout << "Cannot open the config file" <<endl;
        exit(-1);
    }

    string line;

    // Reads through the lines of the file
    int i = 0;
    while (getline(configFile, line))
    {
        switch(i)
        {
        case 0:
            configuration.replacement = line;
            simParams++;
            break;
        case 1:
            configuration.numPages = stoi(line);
            simParams++;
            break;
        case 2:
            // Loops through and grabs the frames from the line
            string frame;
            for (auto chr : line)
            {
                if (chr == ' ')
                {
                    configuration.frames.push_back(stoi(frame, 0, 16));
                    frame = "";
                    simParams++;
                } 
                else
                {
                    frame += chr;
                }
            }
            configuration.frames.push_back(stoi(frame, 0, 16));
            simParams++;
            break;
        }
        
        i++;
    }

    // Sets the size of the pageQueue and all the elements to null
    for (int i = 0; i < configuration.numPages; i++)
    {
        pageQueue.push_back(-1);
    }

    // This will actually hold our free frames, so use won't destroy the data in configuration
    freeFrameList = configuration.frames; 
}

/*Main entry point for function*/
int main(int argc, char **argv)
{
    if (argc == 1)
    {
        cout << "No arguements provided to function" << endl;
        exit(-4);
    }

    //Loops through the command line arguements
    for (int i = 1; i < argc; i++)
    {
        // If the memory reference is set
        if (strcmp(argv[i], "-refs") == 0)
        {
            i++;
            if (i == argc || strcmp(argv[i], "-debug") == 0)
            {
                cout << "-refs must be follwed by a file" << endl;
                exit(-2);
            }
            memFileName = argv[i];
            refs = true;
        }

        // If the debug option is set
        else if (strcmp(argv[i], "-debug") == 0)
        {
            debug = true;
        }

        // Invalid command line arguement
        else 
        {
            cout << "Program ignoring arguement: " << argv[i] << endl;
        }
    }

    // If refs is not set, the program will fail so exit
    if (!refs)
    {
        cout << "No memory reference file provided" << endl;
        exit(-3);
    }

    process_config(); // processes the configuration file to set up page table
    initialize_page_table(); // initializes an empty cache

    if (debug) // displays an empty cache
    {
        cout << "\nInitial Page Table:\n";
        display_page_table();
    }

    process_memFile(); // displays the memory contents

    // Processes each line in the memory file
    for (unsigned int i = 0; i < memFileLines.size(); i++)
    {
        process_line(i);

        // Displays a line
        cout << "\nLine: ";
        cout << hex << setfill('0') << setw(4) << memFileLines[i].virtualAddress << " ";
        cout << memFileLines[i].operation << " ";
        cout << hex << memFileLines[i].pageNumber << " ";
        cout << hex << setfill('0') << setw(3) << memFileLines[i].offset << " ";

        if (memFileLines[i].pageFault) { // Page fault?
            cout << "F ";
        } else {
            cout << "  ";
        }

        if (memFileLines[i].writeBack) { // Writeback?
            cout << "B ";
        } else {
            cout << " ";
        }

        // Creates the physical address
        unsigned int physAdd = memFileLines[i].offset + (memFileLines[i].frame << 11);
        cout << hex << setfill('0') << setw(6) << physAdd;

        cout << "\n";
        if (debug)
        {
            display_page_table();
        }
    }

    // Displays final page table
    cout << "\nFinal Page Table:\n";
    display_page_table();
    cout << "\n";

    // Prints out memory reference count information
    cout << "Count of simulations Parameters: " << simParams << endl;
    cout << "Count of Memory References: " << dec << refCnt << endl;
    cout << "Count of Read Operations: " << dec << readCnt << endl;
    cout << "Count of Write Operations: " << dec << writeCnt << endl;
    cout << "Count of Page Faults: " << dec << pageFaults << endl;
    cout << "Count of Write Backs: " << dec << writeBacks << endl;
    cout << endl;
}
