#pragma once
#include <filesystem>

void map_path(const std::filesystem::path& map_root, const std::filesystem::path& src_root, const std::string& virtual_root);
void link_node(const std::filesystem::path& map_root, const std::filesystem::path& src_path, const std::string& target_path);
void remove_node(const std::filesystem::path& map_root, const std::string& target_path);
void cleanup_map(const std::filesystem::path& map_root,const std::filesystem::path& persistence_root);
std::filesystem::path resolve_map_path(const std::filesystem::path& map_root, const std::string& virtual_path);