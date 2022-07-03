
#include "fat32.h"

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
int file_image;
vector<uint32_t> fat_table;

enum file_type {
    FILE_TYPE_FILE,
    FILE_TYPE_DIRECTORY
};
struct FileRoot {
    string name;
    uint8_t creationTimeMs;        // Creation time down to ms precision
    uint16_t creationTime;
    uint16_t creationDate;
    uint16_t lastAccessTime;       // Last access time
    uint16_t modifiedTime;
    uint16_t modifiedDate;
    uint16_t firstCluster;         // Last two bytes of the first cluster
    uint16_t parentCluster; // Parent directory cluster
    file_type type;                // File type
    uint32_t fileSize;             // Filesize in bytes
    vector <FileRoot> children;
};

FileRoot root;
vector<FileRoot*> current_path;

string get_file_name(string path) {
    string file_name = "";
    int i = path.length() - 1;
    while (path[i] != '/') {
        file_name = path[i] + file_name;
        i--;
    }
    return file_name;
}

//check if a file exists
FileRoot* file_exists(string name, vector<FileRoot> &files) {
    for (int i = 0; i < files.size(); i++) {
        if (files[i].name == name) {
            return &files[i]; //return the file
        }
    }
    return nullptr; //return empty file
}


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

string join(vector<string> strs, string delimiter) {
    string joined = "";
    for (int i = 0; i < strs.size(); i++) {
        joined += strs[i];
        if (i != strs.size() - 1) {
            joined += delimiter;
        }
    }
    return joined;
}

unsigned char lfn_checksum(string name) {
    unsigned char sum = 0;
    for (int i = 0; i < name.length(); i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + name[i];
    }
    return sum;
}

int first_sector_of_cluster(int cluster_number) {
    return (cluster_number - 2) * sectors_per_cluster + first_data_sector;
}

vector<int> get_clusters_of_file(int first_cluster) {
    vector<int> clusters;
    int current_cluster = first_cluster;
    while (current_cluster != 0x0FFFFFF8) {
        clusters.push_back(current_cluster);
        current_cluster = fat_table[current_cluster];
    }
    return clusters;
}

vector<string> divide (string filename) {
    //divide filename to substrings with max length of 13
    vector<string> substrings;
    int i = 0;
    while (i < filename.length()) {
        string substring = filename.substr(i, 13);
        substrings.push_back(substring);
        i += 13;
    }
    return substrings;
}

vector<FatFileEntry> createLFN (string filename) {
    vector<FatFileEntry> lfn;
    vector<string> substrings = divide(filename);
    while (substrings.size() > 0) {
        FatFileEntry lfn_entry = {0};
        string lfnname = substrings.back();
        substrings.pop_back();
        for(int i = 0; i < 5; i++) {
            lfn_entry.lfn.name1[i] = lfnname[i];
        }
        for(int i = 0; i < 6; i++) {
            lfn_entry.lfn.name2[i] = lfnname[i+5];
        }
        for(int i = 0; i < 2; i++) {
            lfn_entry.lfn.name3[i] = lfnname[i+11];
        }
        lfn_entry.lfn.attributes = 0x0F;
        lfn_entry.lfn.reserved = 0x00;
        lfn_entry.lfn.firstCluster = 0x0000;
        lfn.push_back(lfn_entry);
    }
    uint8_t size = lfn.size();
    for(auto &lfn_entry : lfn) {
        lfn_entry.lfn.sequence_number |= size;
        size--;
    }
    lfn[0].lfn.sequence_number |= 0x40; //set the first entry to indicate it is the first entry
    return lfn;
}

vector<FileRoot> get_files(int cluster) {
    vector<FileRoot> files;
    string filename = "";
    vector<int> clusters_of_file = get_clusters_of_file(cluster);
    bool is_file_read_done = false;
    string buffer = "";
    for(auto cluster_num : clusters_of_file) {
        int directory_sector = first_sector_of_cluster(cluster_num);
        int sector_offset = directory_sector * bytes_per_sector;
        lseek(file_image, sector_offset, SEEK_SET);
        for(int j = 0; j < cluster_size/sizeof(FatFileEntry); j++) { //iterate through each entry in the cluster
            FatFileEntry root_directory_entry;
            read(file_image, &root_directory_entry, sizeof(FatFileEntry));
            if(root_directory_entry.msdos.filename[0] == 0) { //if the entry is empty, we're done with this cluster
                break;
            }
            if(root_directory_entry.msdos.filename[0] == 0xE5 || root_directory_entry.msdos.filename[0] == 0x2E ) { //if the entry is deleted, "." or ".." -> skip it
                continue;
            }
            if(root_directory_entry.lfn.attributes == 0x0F) { //if the entry is a long file name entry
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
                buffer = "";
            }
            else {
                if(j != cluster_size/4 - 1) { //if this is the last entry in the cluster, we're done with this cluster. msdos is in the next cluster
                    FileRoot temp;
                    temp.name = filename;
                    temp.creationTimeMs = root_directory_entry.msdos.creationTimeMs;
                    temp.creationTime = root_directory_entry.msdos.creationTime;
                    temp.creationDate = root_directory_entry.msdos.creationDate;
                    temp.lastAccessTime = root_directory_entry.msdos.lastAccessTime;
                    temp.modifiedTime = root_directory_entry.msdos.modifiedTime;
                    temp.modifiedDate = root_directory_entry.msdos.modifiedDate;
                    temp.firstCluster = (root_directory_entry.msdos.eaIndex << 16)| root_directory_entry.msdos.firstCluster;
                    temp.fileSize = root_directory_entry.msdos.fileSize;
                    temp.parentCluster = cluster;
                    if(root_directory_entry.msdos.attributes & 0x10) {
                        temp.type = FILE_TYPE_DIRECTORY;
                    }
                    else {
                        temp.type = FILE_TYPE_FILE;
                    }
                    files.push_back(temp);
                    filename = "";
                }
            }
        }
    }
    return files;
}

vector<FileRoot> get_children_recursive(int cluster_num) {
    vector<FileRoot> children = get_files(cluster_num);
    for(int i = 0; i < children.size(); i++) {
        if(children[i].type == FILE_TYPE_DIRECTORY) {
            children[i].children = get_children_recursive(children[i].firstCluster);
        }
    }
    return children;
}

//read fat32 table to array of ints
void read_fat32() {
    vector<uint32_t> fat;
    int sector_offset = reserved_sectors_count * bytes_per_sector;
    lseek(file_image, sector_offset, SEEK_SET);
    int entry_count = bytes_per_fat/4;
    for(int i = 0; i < entry_count; i++) {
        uint32_t entry;
        read(file_image, &entry, sizeof(uint32_t));
        entry &= 0x0FFFFFFF; //mask the first byte
        fat.push_back(entry);
    }
    fat_table = fat;
}

string get_month_string (uint16_t month) {
    switch(month) {
        case 0:
            return "January";
        case 1:
            return "February";
        case 2:
            return "March";
        case 3:
            return "April";
        case 4:
            return "May";
        case 5:
            return "June";
        case 6:
            return "July";
        case 7:
            return "August";
        case 8:
            return "September";
        case 9:
            return "October";
        case 10:
            return "November";
        case 11:
            return "December";
        default:
            return "ERRONOUS MONTH";
    }
}

string get_time(uint16_t time) {
    //convert time to HH:MM string
    string time_str = "";
    uint16_t hour = time >> 11;
    uint16_t minute = (time & 0x07E0) >> 5;
    time_str += to_string(hour) + ":";
    if(minute < 10) {
        time_str += "0";
    }
    time_str += to_string(minute);
    return time_str;
}

string get_date(uint16_t date) { //TODO: months are incorrect
    //convert date to Month + day string
    string date_str = "";
    uint16_t month = (date & 0x01E0) >> 5;
    uint16_t day = date & 0x001F;
    string month_string = get_month_string(month);
    date_str += month_string + " ";
    if(day < 10) {
        date_str += "0";
    }
    date_str += to_string(day);
    return date_str;
}


int num_of_files_in_path(string path) {
    if(path == "") {
        return root.children.size();
    }
    auto components = split(path, '/');
    vector<FileRoot> children;
    if(components[0] == "") { //absoulte path
        children = root.children;
        components.erase(components.begin());
    }
    else {
        if(current_path.size() == 0) {
            children = root.children;
        }
        else {
            children = current_path.back()->children;
        }
    }
    while(components.size() > 0) {
        string component = components[0];
        components.erase(components.begin());
        for(int i = 0; i < children.size(); i++) {
            if(children[i].name == component) {
                if(components.size() == 0) {
                    return children[i].children.size();
                }
                children = children[i].children;
                break;
            }
        }
        if(children.size() == 0 && components.size() > 0) {
            return -1;
        }
    }
    return -1;
}


int path_exists(string path) {
    //return cluster number of path if it exists, otherwise return -1
    if(path == "") {
        if(current_path.size() == 0) {
            return root.firstCluster;
        }
        else {
            return current_path.back()->firstCluster;
        }
    }
    auto components = split(path, '/');
    vector<FileRoot> children;
    if(components[0] == "") { //absoulte path
        children = root.children;
        components.erase(components.begin());
    }
    else {
        if(current_path.size() == 0) {
            children = root.children;
        }
        else {
            children = current_path.back()->children;
        }
    }
    while(components.size() > 0) {
        string component = components[0];
        components.erase(components.begin());
        for(int i = 0; i < children.size(); i++) {
            if(children[i].name == component) {
                if(components.size() == 0) {
                    return children[i].firstCluster;
                }
                children = children[i].children;
                break;
            }
        }
        if(children.size() == 0 && components.size() > 0) {
            return -1;
        }
    }
    return -1;
}

uint32_t find_new_cluster() {
    for(int i = 2; i < fat_table.size(); i++) {
        if(fat_table[i] == 0) {
            return i;
        }
    }
    return -1;
}

pair<vector<int>, int> allocate_clusters(int cluster_num, int entry_count) {
    vector<int> clusters_to_allocate;
    int allocated_entries = 0;
    int starting_entry;
    int cluster = cluster_num;
    while(true) {
        int directory_sector = first_sector_of_cluster(cluster);
        int sector_offset = directory_sector * bytes_per_sector;
        lseek(file_image, sector_offset, SEEK_SET);
        for(int i = 0; i < cluster_size/sizeof(FatFileEntry); i++) {
            FatFileEntry entry = {};
            read(file_image, &entry, 32);
            if(entry.msdos.filename[0] == 0 || entry.msdos.filename[0] == 0xE5) {
                if(allocated_entries == 0) {
                    starting_entry = i;
                }
                allocated_entries++;
            }
            if(allocated_entries == entry_count) {
                clusters_to_allocate.push_back(cluster);
                return make_pair(clusters_to_allocate, starting_entry);
            }
            if(entry.msdos.filename[0] != 0 && entry.msdos.filename[0] != 0xE5) {
                allocated_entries = 0;
            }
        }
        if(allocated_entries > 0) {
            clusters_to_allocate.push_back(cluster);
        }
        if(cluster == 0x0FFFFFF8) { //we need a new cluster for the file
            uint32_t new_cluster = find_new_cluster();
            fat_table[cluster_num] = new_cluster;
            fat_table[new_cluster] = 0x0FFFFFF8;
            int new_cluster_offset = reserved_sectors_count * bytes_per_sector + new_cluster * 4; 
            int previous_cluster_offset = reserved_sectors_count * bytes_per_sector + cluster_num * 4;
            lseek(file_image, previous_cluster_offset, SEEK_SET);
            write(file_image, &new_cluster, sizeof(uint32_t));
            lseek(file_image, new_cluster_offset, SEEK_SET);
            write(file_image, &fat_table[new_cluster], sizeof(uint32_t));
            cluster = new_cluster;
        }
        else {
            cluster = fat_table[cluster];
        }
    }
}

void cat_file(FileRoot fileroot) {
    int cluster_num = fileroot.firstCluster;
    vector<int> clusters = get_clusters_of_file(cluster_num);
    for(auto cluster : clusters) {
        int sector_offset = first_sector_of_cluster(cluster) * bytes_per_sector;
        lseek(file_image, sector_offset, SEEK_SET);
        for(int i = 0; i < cluster_size/sizeof(char); i++) {
            char c;
            read(file_image, &c, sizeof(char));
            //break if char is eof
            if(c == 0) {
                break;
            }
            cout << c;
        }
    }
}


void change_directory(string path) {
    vector<string> path_components = split(path, '/');
    vector<FileRoot*> current_directory_save = current_path; //save the current directory
    if(path_components.size() == 0) {
        return;
    }

    if(path[0] == '/') { //if the path starts with a /, we're changing to the root directory
        current_path.clear();
        path_components.erase(path_components.begin()); //remove the first element, which is empty because of the '/'
    }

    for(auto component : path_components) {
        if(component == "..") {
            if(current_path.size() > 0) {
                current_path.pop_back();
            }
        }
        else if(component == "."){
            continue;
        }
        else{
            FileRoot* file;
            if(current_path.size() == 0) {
                file = file_exists(component, root.children);
            }
            else {
                file = file_exists(component, current_path.back()->children);
            }

            if(file != nullptr && file->type == FILE_TYPE_DIRECTORY) {
                current_path.push_back(file);
            }
            else {
                current_path = current_directory_save;
                return;
            }
        }
    }
}

void list_directory() {
    if(current_path.size() == 0) { //we are in the root directory
        for(auto child : root.children) {
            cout << child.name << " ";
            if(root.children.back().name == child.name) {
                cout << endl;
            }
        }
    }
    else {
        for(auto child : current_path.back()->children) { //last element in the vector is the current directory
            cout << child.name << " ";
            if(current_path.back()->children.back().name == child.name) {
                cout << endl;
            }
        }
    }
}

void list_directory_long() { //list the directories with long names
    vector<FileRoot> files_to_list;
    if(current_path.size() == 0) { //we are in the root directory
        files_to_list = root.children;
    }
    else {
        files_to_list = current_path.back()->children; //last element in the vector is the current directory
    }

    for(auto child : files_to_list) {
        string last_modification_date = get_date(child.modifiedDate);
        string last_modification_time = get_time(child.modifiedTime);
        if(child.type == FILE_TYPE_DIRECTORY) {
            cout << "drwx------ 1 root root " << child.fileSize << " " << last_modification_date << " " << last_modification_time  << " " << child.name << endl;
        }
        else {
            cout << "-rwx------ 1 root root " << child.fileSize << " " << last_modification_date << " " << last_modification_time << " " << child.name << endl;
        }
    }
}

FatFileEntry create_base_entry(FatFileEntry dot_dot_entry) {
    FatFileEntry entry = {0};
    for(int i = 0; i < 8; i++) {
        entry.msdos.filename[i] = ' ';
    }
    for(int i = 0; i < 3; i++) {
        entry.msdos.extension[i] = ' ';
    }
    entry.msdos.attributes = 0x10; //directory
    int cluster_num = find_new_cluster();
    fat_table[cluster_num] = 0x0FFFFFF8; //mark the cluster as end of file
    int cluster_num_offset_fat1 = reserved_sectors_count * bytes_per_sector + cluster_num * 4; //offset of the cluster number in the FAT table
    int cluster_num_offset_fat2 = reserved_sectors_count * bytes_per_sector + cluster_num * 4 + bytes_per_fat; //offset of the cluster number in the FAT table
    lseek(file_image, cluster_num_offset_fat1, SEEK_SET);
    write(file_image, &fat_table[cluster_num], sizeof(uint32_t)); //write the cluster number to the FAT1 table
    lseek(file_image, cluster_num_offset_fat2, SEEK_SET);
    write(file_image, &fat_table[cluster_num], sizeof(uint32_t)); //write the cluster number to the FAT2 table
    uint16_t high_bytes_cluster = (cluster_num >> 16) & 0xFFFF; //high bytes of the cluster number
    uint16_t low_bytes_cluster = cluster_num & 0xFFFF; //low bytes of the cluster number
    entry.msdos.eaIndex = high_bytes_cluster;
    entry.msdos.firstCluster = low_bytes_cluster;
    FatFileEntry dot_entry = {0};
    dot_entry.msdos = entry.msdos;
    dot_entry.msdos.filename[0] = '.';
    dot_entry.msdos.filename[1] = ' ';
    dot_entry.msdos.filename[2] = ' ';
    dot_entry.msdos.filename[3] = ' ';
    dot_entry.msdos.filename[4] = ' ';
    dot_entry.msdos.filename[5] = ' ';
    dot_entry.msdos.filename[6] = ' ';
    dot_entry.msdos.filename[7] = ' ';
    dot_entry.msdos.extension[0] = ' ';
    dot_entry.msdos.extension[1] = ' ';
    dot_entry.msdos.extension[2] = ' ';

    //write dot and dot dot entries to the cluster
    int cluster_offset = first_sector_of_cluster(cluster_num) * bytes_per_sector; //offset of the cluster in the file image
    lseek(file_image, cluster_offset, SEEK_SET);
    write(file_image, &dot_entry, sizeof(FatFileEntry));
    write(file_image, &dot_dot_entry, sizeof(FatFileEntry));
    return entry;
}

FatFileEntry find_dot_dot(int cluster) {
    FatFileEntry entry = {0};
    if(cluster == 2) { //root directory is in cluster 2
        return entry;
    }
    else {
        int directory_sector = first_sector_of_cluster(cluster);
        int directory_sector_offset = directory_sector * bytes_per_sector;
        lseek(file_image, directory_sector_offset, SEEK_SET);
        read(file_image, &entry, sizeof(FatFileEntry)); //first entry in the directory is the directroy base
    }
    return entry;
}

//separate an integer to its digits
vector<int> separate_int(int num) {
    vector<int> digits;
    while(num > 0) {
        digits.push_back(num % 10);
        num /= 10;
    }
    return digits;
}


void make_directory(string filename) {
    if(filename == "") {
        return;
    }
    auto components = split(filename, '/');
    filename = filename.substr(1); //remove the first character, which is '/'
    filename = components.back(); //get the last component of the path
    components.pop_back(); //remove the last component of the path
    string path = join(components, "/"); //recreate the path
    int path_cluster_num = path_exists(path);
    if(path_cluster_num == -1) {
        return;
    }
    FatFileEntry dot_dot_entry = find_dot_dot(path_cluster_num); //return .. entry
    dot_dot_entry.msdos.filename[0] = '.';
    dot_dot_entry.msdos.filename[1] = '.'; //set the filename to ..
    dot_dot_entry.msdos.filename[2] = ' ';
    dot_dot_entry.msdos.filename[3] = ' ';
    dot_dot_entry.msdos.filename[4] = ' ';
    dot_dot_entry.msdos.filename[5] = ' ';
    dot_dot_entry.msdos.filename[6] = ' ';
    dot_dot_entry.msdos.filename[7] = ' ';
    dot_dot_entry.msdos.extension[0] = ' ';
    dot_dot_entry.msdos.extension[1] = ' ';
    dot_dot_entry.msdos.extension[2] = ' ';
    dot_dot_entry.msdos.attributes = 0x10; //set the attributes to directory
    auto entries = createLFN(filename);
    FatFileEntry base = create_base_entry(dot_dot_entry); //create the base entry for the directory
    int filenum = num_of_files_in_path(path);
    base.msdos.filename[0] = '~';
    vector<int> numbers = separate_int(filenum+1);
    for(int i = 0; i < numbers.size(); i++) {
        base.msdos.filename[i+1] = to_string(numbers[i])[0];
    }
    string checksumname;
    for(int i = 0; i < 8; i++) {
        checksumname += base.msdos.filename[i];
    }
    for(int i = 0; i < 3; i++) {
        checksumname += base.msdos.extension[i];
    }
    int checksum = lfn_checksum(checksumname);
    for(auto &lfn : entries) {
        lfn.lfn.checksum = checksum;
    }
    auto clusters = allocate_clusters(path_cluster_num ,entries.size()+1);
    vector<int> allocated_clusters = clusters.first;
    int starting_entry = clusters.second;
    
    for(int i = 0; i < allocated_clusters.size(); i++) {
        int cluster_num = allocated_clusters[i];
        int cluster_num_offset = first_sector_of_cluster(cluster_num) * bytes_per_sector + sizeof(FatFileEntry) * starting_entry; //offset of the cluster in the file image
        lseek(file_image, cluster_num_offset, SEEK_SET);
        for(int j = 0; j < entries.size(); j++) {
            write(file_image, &entries[j], sizeof(FatFileEntry));
        }
    }
    write(file_image, &base, sizeof(FatFileEntry));
    root.children = get_children_recursive(2);

}

void touch(string filename) {

}

void move_file(string filename, string path_to_move) {

}

void cat(string filename) {
    if(filename == "") {
        return;
    }
    if(filename[0] == '/') { //if the filename starts with a '/', it is a path
        auto components = split(filename, '/');
        filename = components.back();
        components.pop_back(); //remove the filename
        string path = join(components, "/");
        vector<FileRoot*> restore_path = current_path; //save the current path
        if(path != "") {
            change_directory(path);
        }
        else {
            change_directory("/");
        }
        FileRoot* file;
        if(current_path.size() == 0) {
            file = file_exists(filename, root.children);
        }
        else {
            file = file_exists(filename, current_path.back()->children);
        }
        if(file != nullptr) {
            cat_file(*file);
        }
        current_path = restore_path; //restore the current path

    }
    else{
        FileRoot* file;
        auto components = split(filename, '/');
        filename = components.back();
        components.pop_back(); //remove the filename
        string path = join(components, "/");
        auto restore_path = current_path; //save the current path
        if(path != "") {
            change_directory(path);
        }
        else {
            change_directory("/");
        }

        if(current_path.size() == 0) { //we are in the root directory
            file = file_exists(filename, root.children);
        }
        else { //we are in a subdirectory
            file = file_exists(filename, current_path.back()->children);
        }

        if(file != nullptr) { //if the file exists
            cat_file(*file); 
        }
        current_path = restore_path; //restore the current path
    }
}







int main(int argc, char *argv[]) {
    //open the file image with all permissions
    file_image = open(argv[1], O_RDWR);
    BPB_struct bpb;
    //map the all file image to memory with mmap
    read(file_image, &bpb, sizeof(BPB_struct));

    //take neccessary information from the BPB
    sectors_per_cluster = bpb.SectorsPerCluster;
    reserved_sectors_count = bpb.ReservedSectorCount;
    cluster_size = bpb.SectorsPerCluster * bpb.BytesPerSector;
    first_data_sector = reserved_sectors_count + (bpb.NumFATs * bpb.extended.FATSize);
    bytes_per_sector = bpb.BytesPerSector;
    bytes_per_fat = bpb.extended.FATSize * bpb.BytesPerSector;
    read_fat32(); //read fat32 table to array of ints

    //create root directory
    int root_directory_cluster_number = bpb.extended.RootCluster;
    root.name = "root";
    root.children = get_children_recursive(root_directory_cluster_number); //get the children of the root directory


    //main loop
    while (1) {
        //print the current path to the screen
        string path = "/";
        for(int i = 0; i < current_path.size(); i++) {
            path += current_path[i]->name;
            if(i != current_path.size() - 1) {
                path += "/";
            }
        }
        cout << path << "> ";

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
            if(tokens.size() == 2) {
                change_directory(tokens[1]);
            }
        }

        //check if the first token is ls
        if (tokens[0] == "ls") {
            vector<FileRoot*> current_directory = current_path; //save the current directory
            if(tokens.size() == 2) { //path or -l given
                if(tokens[1] == "-l") {
                    list_directory_long();
                }
                else {
                    change_directory(tokens[1]);
                    list_directory();
                    current_path = current_directory; //restore the current directory
                }
            }
            else if(tokens.size() == 3) { //path and "-l" given
                change_directory(tokens[2]);
                list_directory_long();
                current_path = current_directory; //restore the current directory
            }
            else { //no path given
                list_directory();
            }
        }

        //check if the first token is mkdir
        if (tokens[0] == "mkdir") {
            make_directory(tokens[1]);
        }

        //check if the first token is touch
        if (tokens[0] == "touch") {
            touch(tokens[1]);
        }

        //check if the first token is mv
        if (tokens[0] == "mv") {
            move_file(tokens[1], tokens[2]);
        }
        
        //check if the first token is cat
        if (tokens[0] == "cat") {
            cat(tokens[1]);
        }

        //check if the first token is quit
        if (tokens[0] == "quit") {
            break;
        }
    }
    return 0;
}
