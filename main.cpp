#include <iostream>
#include <fstream>
#include <string.h>
#include <vector>
#include <iterator>
#include <algorithm>
#include <map>

#include <sys/stat.h>

#include "common.h"
#include "parsing_types.h"
#include "ini_processing.h"

struct processing_settings
{
    processing_settings()
    {
        firmware_file_path = nullptr;
        firmware_folder_path = nullptr;
        running_mode = none;
    }
    
    char* firmware_file_path;
    char* firmware_folder_path;

    enum running_mode_enum { none, pack, unpack };
    running_mode_enum running_mode;
};

void print_prologue()
{
    std::cout << "=== Storyboard Creativity ===" << std::endl;
    std::cout << storyboard_creativity_logo << std::endl << std::endl;
}
void print_usage(char* arg0)
{
    std::cout << "Usage:" << std::endl << std::endl;
    std::cout << "== To unpack firmware ==" << std::endl;
    std::cout << arg0 << " -u <firmware_file_path>" << std::endl << std::endl;
    std::cout << "== To pack firmware ==" << std::endl;
    std::cout << arg0 << " -p <unpacked_firmware_folder_path>" << std::endl << std::endl;
}

bool process_unpacking_params(processing_settings& settings, int argc, char* argv[], int& i)
{
    if (i >= argc)
        return true;
    
    if (argv[i][0] != '-')
    {
        std::cout << "Wrong parameter " << argv[i] << " ('-' symbol expected)." << std::endl;
        return false;
    }
    
    if (strcmp(argv[i], "-u") != 0)
        return true;

    if (settings.running_mode != processing_settings::running_mode_enum::none)
    {
        std::cout << "Multiple processing modes are not allowed." << std::endl;
        return false;
    }

    settings.running_mode = processing_settings::running_mode_enum::unpack;

    if (i + 1 >= argc)
    {
        std::cout << "Missing firmware file path." << std::endl;
        return false;
    }

    settings.firmware_file_path = argv[i + 1];
    i += 2;

    return true;
}
bool process_packing_params(processing_settings& settings, int argc, char* argv[], int& i)
{
    if (i >= argc)
        return true;

    if (argv[i][0] != '-')
    {
        std::cout << "Wrong parameter " << argv[i] << " ('-' symbol expected)." << std::endl;
        return false;
    }
    
    if (strcmp(argv[i], "-p") != 0)
        return true;

    if (settings.running_mode != processing_settings::running_mode_enum::none)
    {
        std::cout << "Multiple processing modes are not allowed." << std::endl;
        return false;
    }

    settings.running_mode = processing_settings::running_mode_enum::pack;

    if (i + 1 >= argc)
    {
        std::cout << "Missing firmware folder path." << std::endl;
        return false;
    }

    settings.firmware_folder_path = argv[i + 1];
    i += 2;

    return true;
}
bool process_help_params(processing_settings& settings, int argc, char* argv[], int& i)
{
    if (i >= argc)
        return true;
    
    if (argv[i][0] != '-')
    {
        std::cout << "Wrong parameter " << argv[i] << " ('-' symbol expected)." << std::endl;
        return false;
    }
    
    if (strcmp(argv[i], "-h") != 0 || strcmp(argv[i], "--help") == 0)
    {
        ++i;
        return false;
    }

    return true;
}

bool check_parameters(processing_settings& settings)
{
    switch(settings.running_mode)
    {
        case processing_settings::running_mode_enum::none:
            std::cout << "No any mode was chosen." << std::endl;
            return false;

        case processing_settings::running_mode_enum::unpack:
            std::cout << "Firmware unpack mode enabled." << std::endl;
            if  (settings.firmware_file_path == nullptr)
            {
                std::cout << "No firmware file was chosen!" << std::endl;
                return false;
            }
            std::cout << "Firmware file: " << settings.firmware_file_path << std::endl;
            break;

        case processing_settings::running_mode_enum::pack:
            std::cout << "Firmware pack mode enabled." << std::endl;
            if  (settings.firmware_folder_path == nullptr)
            {
                std::cout << "No firmware folder was chosen!" << std::endl;
                return false;
            }
            std::cout << "Firmware folder: " << settings.firmware_folder_path << std::endl;
            break;
    }
    return true;
}

bool parsing_process_unpack_partition(ini_info_t& ini_info, std::ifstream& fw_file_stream, firmware_file_header &fw_header, int partition_index)
{
    // Reading partiton header
    partition_header partition_header;
    fw_file_stream.read((char*)&partition_header, sizeof(partition_header));

    ini_info[std::string(INI_SECTION_PREFIX__PARTITION) + partition_names[partition_index]][INI_SECTION__PARTITION__OFFSET_IN_MEMORY] = std::to_string(partition_header.offset_in_device_memory);

    std::cout << std::hex;
    std::cout << "CRC32: 0x" << partition_header.crc32 << std::endl;
    std::cout << "Data Size: 0x" << partition_header.data_size << std::endl;
    std::cout << "Offset in device memory: 0x" << partition_header.offset_in_device_memory << std::endl;
    std::cout << "Magic: 0x" << partition_header.magic << std::endl;
    std::cout << std::dec;

    if (partition_header.magic != PARTITION_HEADER_MAGIC)
    {
        std::cout << "Error! Partition magic is not 0x" << std::hex << PARTITION_HEADER_MAGIC << std::dec << std::endl;
        return false;
    }

    ini_info[std::string(INI_SECTION_PREFIX__PARTITION) + partition_names[partition_index]][INI_SECTION__PARTITION__SOURCE_FILE_NAME] = std::string(partition_names[partition_index]) + ".bin";
    auto fw_partition_file_path = std::string(output_folder_name) + '/' + partition_names[partition_index] + ".bin";
    std::ofstream fw_partition_stream(fw_partition_file_path, std::ios::out | std::ios::binary);
    if (!fw_partition_stream.is_open())
    {
        std::cout << "Error! Could not open file " << fw_partition_file_path << " for write. Skipping partition..." << std::endl;

        // Skip partition data
        fw_file_stream.seekg(fw_file_stream.tellg() + partition_header.data_size);

        fw_partition_stream.close();
        return true;
    }

    uint8_t* partition_data = new uint8_t[partition_header.data_size];
    fw_file_stream.read((char*)partition_data, partition_header.data_size);
    fw_partition_stream.write((char*)partition_data, partition_header.data_size);
    delete [] partition_data;

    fw_partition_stream.close();
    return true;
}

void parsing_process_unpack(char* fw_file_path)
{
    std::cout << std::endl << "=== Unpacking process ===" << std::endl << std::endl;

    // Output INI data
    ini_info_t ini_info;

    // Opening firmware file
    std::ifstream fw_file_stream(fw_file_path, std::ios::in | std::ios::binary);
    if (!fw_file_stream.is_open())
    {
        std::cout << "Error! Could not open file " << fw_file_path << std::endl;
        fw_file_stream.close();
        return;
    }

    // Reading firmware file header
    firmware_file_header fw_header;
    fw_file_stream.read((char*)&fw_header, sizeof(firmware_file_header));

    if (fw_header.model_name[sizeof(fw_header.model_name) - 1] != '\0')
    {
        std::cout << "Error! Model name is corrupted!" << std::endl;
        fw_file_stream.close();
        return;
    }

    ini_info[INI_SECTION__FW_HEADER][INI_SECTION__FW_HEADER__MODEL_NAME] = std::string((char*)fw_header.model_name);
    std::cout << "Model name: " << fw_header.model_name << std::endl;

    ini_info[INI_SECTION__FW_HEADER][INI_SECTION__FW_HEADER__VERSION_MAJOR] = std::to_string(fw_header.version_major);
    ini_info[INI_SECTION__FW_HEADER][INI_SECTION__FW_HEADER__VERSION_MINOR] = std::to_string(fw_header.version_minor);

    std::cout << "Firmware version: " << fw_header.version_major << '.' << fw_header.version_minor << std::endl << std::endl;

    std::vector<int> partition_indexes;
    for (int i = 0; i < FW_PARTITION_COUNT; ++i)
    {
        ini_info[std::string(INI_SECTION_PREFIX__PARTITION) + partition_names[i]][INI_SECTION__PARTITION__ENABLED] = "0";

        std::cout << "> Partition " << i << " (" << partition_names[i] << "):" << std::hex << std::endl;
        std::cout << "Size in FW file: 0x" << fw_header.partition_infos[i].size_in_fw_file << std::endl;
        std::cout << "Size in memory:  0x" << fw_header.partition_sizes_in_memory[i] << std::endl;
        std::cout << "CRC32:           0x" << fw_header.partition_infos[i].crc32 << std::endl;
        std::cout << std::dec << std::endl;

        if (fw_header.partition_infos[i].size_in_fw_file != 0)
        {
            ini_info[std::string(INI_SECTION_PREFIX__PARTITION) + partition_names[i]][INI_SECTION__PARTITION__ENABLED] = "1";
            ini_info[std::string(INI_SECTION_PREFIX__PARTITION) + partition_names[i]][INI_SECTION__PARTITION__SIZE_IN_MEMORY] = std::to_string(fw_header.partition_sizes_in_memory[i]);

            partition_indexes.push_back(i);
        }
    }

    for (int i = 0; i < 7; ++i)
    {
        ini_info[INI_SECTION__UNKNOWN_DATA]["u" + std::to_string(i)] = std::to_string(fw_header.unknown[i]);
        std::cout << std::hex << fw_header.unknown[i] << ' ' << std::endl;
    }

    std::cout << std::endl << "Header CRC32: 0x" << fw_header.crc32 << std::endl;

    if (partition_indexes.size() == 0)
    {
        std::cout << "No partitions are present to be unpacked." << std::endl;
        fw_file_stream.close();
        return;
    }

    // Creating output folder
    if (mkdir(output_folder_name, S_IRWXU | S_IRWXG | S_IRWXO) != 0 && errno != EEXIST)
    {
        std::cout << "Error! Could not create output folder " << output_folder_name << "!" << std::endl;
        std::cout << "Reason: ";
        switch (errno)
        {
        case EACCES:
            std::cout << "EACCES";
            break;

        case ELOOP:
            std::cout << "ELOOP";
            break;

        case EMLINK:
            std::cout << "EMLINK";
            break;

        case ENAMETOOLONG:
            std::cout << "ENAMETOOLONG";
            break;

        case ENOENT:
            std::cout << "ENOENT";
            break;

        case ENOSPC:
            std::cout << "ENOSPC";
            break;

        case ENOTDIR:
            std::cout << "ENOTDIR";
            break;

        case EROFS:
            std::cout << "EROFS";
            break;
        
        default:
            std::cout << "Unknown error";
            break;
        }
        fw_file_stream.close();
        return;
    }
    std::cout << "Created output folder (" << output_folder_name << ")." << std::endl;

    for (int i = 0; i < partition_indexes.size(); ++i)
    {
        std::cout << "=== [Unpacking partition : " << partition_names[partition_indexes[i]] << "] ===" << std::endl << std::endl;

        auto pre_pos = fw_file_stream.tellg();
        std::cout << ">> Partition starts at offset: " << pre_pos << std::endl;
        std::cout << ">> Expecting partition end offset: " << (uint32_t)pre_pos + fw_header.partition_infos[partition_indexes[i]].size_in_fw_file << std::endl;

        if(!parsing_process_unpack_partition(ini_info, fw_file_stream, fw_header, partition_indexes[i]))
        {
            std::cout << "Error occured while parsing partition." << std::endl;
            fw_file_stream.close();
            return;
        }

        std::cout << ">> Partition actual end offset: " << fw_file_stream.tellg() << std::endl << std::endl;

        if (fw_file_stream.tellg() != (uint32_t)pre_pos + fw_header.partition_infos[partition_indexes[i]].size_in_fw_file)
        {
            std::cout << "Error! Partition borders are corrupted in header!" << std::endl;
            std::cout << "Expected partition end offset: " << (uint32_t)pre_pos + fw_header.partition_infos[partition_indexes[i]].size_in_fw_file << std::endl;
            fw_file_stream.close();
            return;
        }
    }

    // Save INI file
    ini_save(ini_info, std::string(output_folder_name) + '/' + unpacked_firmware_settings_name);

    fw_file_stream.close();
}

bool is_number(const std::string& s)
{
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

bool stoi_nothrow(std::string str, uint32_t& out)
{
    try
    {
        if (!is_number(str))
            return false;
        out = std::stoi(str);
    }
    catch(const std::exception& e)
    {
        return false;
    }

    return true;
}

void parsing_process_pack(char* fw_folder_path)
{
    std::cout << std::endl << "=== Packing process ===" << std::endl << std::endl;

    // Input INI data
    ini_info_t ini_info = ini_load(std::string(fw_folder_path) + '/' + unpacked_firmware_settings_name);
    
    if (ini_info.find(INI_SECTION__FW_HEADER) == ini_info.end())
    {
        std::cout << "Error! No [" << INI_SECTION__FW_HEADER << "] section found." << std::endl;
        return;
    }

    auto& ini_fw_header = ini_info[INI_SECTION__FW_HEADER];
    if (ini_fw_header.find(INI_SECTION__FW_HEADER__MODEL_NAME) == ini_fw_header.end())
    {
        std::cout << "Warning! No \"" << INI_SECTION__FW_HEADER__MODEL_NAME << "\" parameter found. Using default : \"" << default_model_name << "\"." << std::endl;
        ini_fw_header[INI_SECTION__FW_HEADER__MODEL_NAME] = default_model_name;
    }

    auto model_name = ini_fw_header[INI_SECTION__FW_HEADER__MODEL_NAME];
    if (model_name != default_model_name)
        std::cout << "Warning! Custom model name is used (" << model_name << ")! Be careful!" << std::endl;

    if (model_name.size() > 31)
    {
        std::cout << "Error! Model name is too long (" << model_name << ")! Only 31 symbols are allowed!" << std::endl;
        return;
    }

    // Show model name
    std::cout << "Model name: " << model_name << std::endl;

    if (ini_fw_header.find(INI_SECTION__FW_HEADER__VERSION_MAJOR) == ini_fw_header.end())
    {
        std::cout << "Error! Major version parameter (" << INI_SECTION__FW_HEADER__VERSION_MAJOR << ") not found!" << std::endl;
        return;
    }

    if (ini_fw_header.find(INI_SECTION__FW_HEADER__VERSION_MINOR) == ini_fw_header.end())
    {
        std::cout << "Error! Minor version parameter (" << INI_SECTION__FW_HEADER__VERSION_MAJOR << ") not found!" << std::endl;
        return;
    }

    uint32_t v_maj = -1, v_min = -1;
    if (!stoi_nothrow(ini_fw_header[INI_SECTION__FW_HEADER__VERSION_MAJOR], v_maj))
    {
        std::cout << "Error! Could not interpret firmware major version (" << ini_fw_header[INI_SECTION__FW_HEADER__VERSION_MAJOR] << ") as integer!" << std::endl;
        return;
    }
    if (!stoi_nothrow(ini_fw_header[INI_SECTION__FW_HEADER__VERSION_MINOR], v_min))
    {
        std::cout << "Error! Could not interpret firmware minor version (" << ini_fw_header[INI_SECTION__FW_HEADER__VERSION_MINOR] << ") as integer!" << std::endl;
        return;
    }

    // Show version
    std::cout << "Version: " << v_maj << '.' << v_min << std::endl;

    
}

void parsing_process(processing_settings& settings)
{
    switch (settings.running_mode)
    {
    case processing_settings::running_mode_enum::none:
        break;

    case processing_settings::running_mode_enum::unpack:
        parsing_process_unpack(settings.firmware_file_path);
        break;

    case processing_settings::running_mode_enum::pack:
        parsing_process_pack(settings.firmware_folder_path);
        break;
    
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
    print_prologue();

    processing_settings settings;

    int i = 1;
    while (i < argc)
    {
        int before = i;
        if (!process_unpacking_params(settings, argc, argv, i) ||
            !process_packing_params(settings, argc, argv, i) ||
            !process_help_params(settings, argc, argv, i))
        {
            print_usage(argv[0]);
            return -1;
        }

        if (before == i)
        {
            std::cout << "Could not parse parameter " << argv[i] << std::endl;
            print_usage(argv[0]);
            return -2;
        }
    }

    if (!check_parameters(settings))
    {
        print_usage(argv[0]);
        return -3;
    }

    parsing_process(settings);

    return 0;
}