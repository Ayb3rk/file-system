extern "C" {
#include "fat32.h"
}
#include <stdint.h>
#include <stdio.h>
#include <sstream> //only used for input parsing
#include <iostream>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <vector>

using namespace std;
int sectors_per_cluster;
int reserved_sectors_count;
int cluster_size;
int first_data_sector;
int bytes_per_sector;
int bytes_per_fat;
string current_path = "/";

string get_file_name(string path) {
  string file_name = "";
  int i = path.length() - 1;
  while (path[i] != '/') {
    file_name = path[i] + file_name;
    i--;
  }
  return file_name;
}

struct FileRoot {
    string name;
    uint8_t creationTimeMs;        // Creation time down to ms precision
    uint16_t creationTime;         // Creation time with H:M:S format
    uint16_t creationDate;         // Creation date with Y:M:D format
    uint16_t lastAccessTime;       // Last access time
    uint16_t eaIndex;              // Used to store first two bytes of the first cluster
    uint16_t modifiedTime;         // Modification time with H:M:S format
    uint16_t modifiedDate;         // Modification date with Y:M:D format
    uint16_t firstCluster;         // Last two bytes of the first cluster
    uint32_t fileSize;             // Filesize in bytes
    vector <FileRoot> children;
};

//a split function to split a string into a vector of strings
vector<string> split(string str, char delimiter) {
  vector<string> internal;
  stringstream ss(str); // Turn the string into a stream.
  string tok;
    while (getline(ss, tok, delimiter)) {
        internal.push_back(tok);
    }
    return internal;
}

unsigned char lfn_checksum(const unsigned char *pFCBName)
{
   int i;
   unsigned char sum = 0;

   for (i = 11; i; i--)
      sum = ((sum & 1) << 7) + (sum >> 1) + *pFCBName++;

   return sum;
}

int first_sector_of_cluster(int cluster_number) {
    return (cluster_number - 2) * sectors_per_cluster + first_data_sector;
}

vector<FileRoot> get_files(int cluster_num, int file_image) {
    vector<FileRoot> files;
    int directory_sector = first_sector_of_cluster(cluster_num);
    int sector_offset = directory_sector * bytes_per_sector;
    lseek(file_image, sector_offset, SEEK_SET);
    string buffer = "";
    string filename = "";
    for(int j = 0; j < cluster_size/32; j++) { //iterate through each entry in the cluster
        buffer = "";
        FatFileEntry root_directory_entry;
        read(file_image, &root_directory_entry, sizeof(FatFileEntry));

        if(root_directory_entry.msdos.filename[0] == 0) {
            break;
        }
        
        for(int i = 0; i < 13; i++) {
            if(i < 5) {
                if(root_directory_entry.lfn.name1[i] == 0) {
                    break;
                }
                buffer += (char) root_directory_entry.lfn.name1[i];
            }
            else if(i < 11) {
                if(root_directory_entry.lfn.name2[i - 5] == 0) {
                    break;
                }
                buffer += (char) root_directory_entry.lfn.name2[i - 5];
            }
            else if(i < 13) {
                if(root_directory_entry.lfn.name3[i - 11] == 0) {
                    break;
                }
                buffer += (char) root_directory_entry.lfn.name3[i - 11];
            }
        }
        filename = buffer + filename;
        if(root_directory_entry.lfn.sequence_number & 0x01) {
            FileRoot temp;
            read(file_image, &root_directory_entry, sizeof(FatFileEntry)); //go to base directory entry, lfns end
            temp.name = filename;
            temp.creationTimeMs = root_directory_entry.msdos.creationTimeMs;
            temp.creationTime = root_directory_entry.msdos.creationTime;
            temp.creationDate = root_directory_entry.msdos.creationDate;
            temp.lastAccessTime = root_directory_entry.msdos.lastAccessTime;
            temp.eaIndex = root_directory_entry.msdos.eaIndex;
            temp.modifiedTime = root_directory_entry.msdos.modifiedTime;
            temp.modifiedDate = root_directory_entry.msdos.modifiedDate;
            temp.firstCluster = root_directory_entry.msdos.firstCluster;
            temp.fileSize = root_directory_entry.msdos.fileSize;
            files.push_back(temp);
            filename = "";
        }
    }
    return files;
}

//read fat32 table to array of ints, make it big endian and return it
vector<int> read_fat32(int file_image) {
    vector<int> fat;
    int sector_offset = reserved_sectors_count * bytes_per_sector;
    lseek(file_image, sector_offset, SEEK_SET);
    
}


void change_directory(string path) {

}

void list_directory() {

}

void make_directory(string filename, string current_path) {

}

void touch(string filename, string current_path) {

}

void move_file(string filename, string path_to_move, string current_path) {

}

void cat(string filename, string current_path) {

}







int main(int argc, char *argv[]) {
    //open the file image with all permissions
    int file_image = open(argv[1], O_RDWR);
    BPB_struct bpb;
    //map the all file image to memory with mmap
    read(file_image, &bpb, sizeof(BPB_struct));
    FileRoot root;
    root.name = "root";

    //take neccessary information from the BPB
    sectors_per_cluster = bpb.SectorsPerCluster;
    reserved_sectors_count = bpb.ReservedSectorCount;
    cluster_size = bpb.SectorsPerCluster * bpb.BytesPerSector;
    first_data_sector = reserved_sectors_count + (bpb.NumFATs * bpb.extended.FATSize);
    bytes_per_sector = bpb.BytesPerSector;
    bytes_per_fat = bpb.extended.FATSize * bpb.BytesPerSector;



    int root_directory_cluster_number = bpb.extended.RootCluster;
    //get the root directory sector
    vector<FileRoot> files = get_files(root_directory_cluster_number, file_image);


    for (auto file : files) {
        cout << file.name << endl;
    }






    
    













    






    while (1) {
        //print the current path to the screen
        printf("%s> ", current_path.c_str());

        //create a string to store the input
        string input;

        //get the input
        getline(cin, input);

        //split the input into tokens
        vector<string> tokens = split(input, ' ');

        //check if the input is empty
        if (tokens.size() == 0) {
            continue;
        }
        
        //check if the first token is cd
        if (tokens[0] == "cd") {
            change_directory(tokens[1]);
        }

        //check if the first token is ls
        if (tokens[0] == "ls") {
            list_directory();
        }

        //check if the first token is mkdir
        if (tokens[0] == "mkdir") {
            make_directory(tokens[1], current_path);
        }

        //check if the first token is touch
        if (tokens[0] == "touch") {
            touch(tokens[1], current_path);
        }

        //check if the first token is mv
        if (tokens[0] == "mv") {
            move_file(tokens[1], tokens[2], current_path);
        }
        
        //check if the first token is cat
        if (tokens[0] == "cat") {
            cat(tokens[1], current_path);
        }

        //check if the first token is quit
        if (tokens[0] == "quit") {
            break;
        }
    }
    return 0;
}
