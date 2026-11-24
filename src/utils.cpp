// Random file and string related operations.
#include <stdio.h>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include "zip_file.hpp"

std::string read_file(const std::string& path_to_file){
    std::string file_data;
    std::ifstream t(path_to_file);
    if(!t){return file_data;}

    t.seekg(0, std::ios::end);   
    file_data.reserve(t.tellg());
    t.seekg(0, std::ios::beg);

    file_data.assign((std::istreambuf_iterator<char>(t)),
            std::istreambuf_iterator<char>());
    
    return file_data;
}

void write_lines_to_file(std::vector<std::string>& lines, const std::string& output_path){
    std::ofstream ofile;
    ofile.open(output_path);
    for (auto & line : lines) {
        ofile.write(line.c_str(),line.length());
        ofile.write("\n",1);
    }
    ofile.close();
}

void write_string_to_file(const std::string& data, const std::string& output_path){
    std::ofstream ofile;
    ofile.open(output_path);
    ofile.write(data.c_str(),data.length());
    ofile.close();
}

std::string str_replace(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

std::vector<std::string> split_string(const std::string& str, const std::string& delimiter){
    std::vector<std::string> strings;

    std::string::size_type pos = 0;
    std::string::size_type prev = 0;
    while ((pos = str.find(delimiter, prev)) != std::string::npos){
        strings.push_back(str.substr(prev, pos - prev));
        prev = pos + 1;
    }

    // To get the last substring (or only, if delimiter is not found)
    strings.push_back(str.substr(prev));

    return strings;
}

std::vector<std::string> read_lines_from_file(const std::string& path_to_file){
    std::string fdata = read_file(path_to_file);
    // Remove CRs
    fdata = str_replace(fdata,"\r","");
    return split_string(fdata,"\n");
}

std::string get_path_extension(const std::string& s) {
   size_t i = s.rfind('.', s.length());
   if (i != std::string::npos) {
      return(s.substr(i+1, s.length() - i));
   }
   return("");
}

std::string lowercase(const std::string& s){
    std::string data = s;
    std::transform(data.begin(), data.end(), data.begin(),[](unsigned char c){ return std::tolower(c); });
    return data;
}

int extract_zip_file(const std::string& path_to_zipfile, const std::string& dest_path){
    try {
        std::filesystem::create_directories(dest_path);
        miniz_cpp::zip_file file(path_to_zipfile);

        // miniz_cpp writes files but never creates folders, and many ZIPs
        // carry no directory entries, so make every entry's parent first.
        for(auto const& info : file.infolist()){
            std::filesystem::path p = dest_path;
            p.append(info.filename);
            std::filesystem::create_directories(p.parent_path());
        }
        file.extractall(dest_path);
        return 1;
    } catch (const std::exception& e) {
        printf("ZIP extraction failed for %s: %s\n", path_to_zipfile.c_str(), e.what());
        return 0;
    }
}