#pragma once
#include <string>
#include <filesystem>

int add_mount(std::string& image_filename, std::filesystem::path& image_path, std::filesystem::path& mount_path);
void rollback_mounts();
void save_mounts_conf(const std::string& output_path);
void remove_mounts_from_conf(const std::string& conf_path);
