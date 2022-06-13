extern "C" {
#include "fat32.h"
}
#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace std;

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




void change_directory(string path) {

}

void list_directory(string current_path) {

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
            list_directory(current_path);
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
    }
    return 0;
}
