#pragma once
#include <string>
#include <vector>

std::string str_replace(std::string str, const std::string& from, const std::string& to);
std::vector<std::string> split_string(const std::string& str, const std::string& delimiter);
void write_lines_to_file(std::vector<std::string>& lines, const std::string& output_path);
void write_string_to_file(const std::string& data, const std::string& output_path);
std::string read_file(const std::string& path_to_file);
std::vector<std::string> read_lines_from_file(const std::string& path_to_file);
std::string get_path_extension(const std::string& s);
std::string lowercase(const std::string& data);
int extract_zip_file(const std::string& path_to_zipfile, const std::string& dest_path);