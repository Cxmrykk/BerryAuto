#include "config_parser.hpp"
#include "globals.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

void load_config()
{
    const char* home = getenv("HOME");
    std::string paths[] = {home ? std::string(home) + "/.config/berryauto.conf" : "", "/etc/berryauto.conf",
                           "berryauto.conf"};

    for (const auto& path : paths)
    {
        if (path.empty())
            continue;

        std::ifstream file(path);
        if (file.is_open())
        {
            LOG_I("Loading configuration from: " << path);
            std::string line;
            while (std::getline(file, line))
            {
                // Remove comments
                auto comment_pos = line.find('#');
                if (comment_pos != std::string::npos)
                    line = line.substr(0, comment_pos);

                // Remove whitespace
                line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
                if (line.empty())
                    continue;

                auto delim = line.find('=');
                if (delim == std::string::npos)
                    continue;

                std::string key = line.substr(0, delim);
                std::string val = line.substr(delim + 1);

                try
                {
                    if (key == "video_encoder")
                        user_config_video_encoder = val;
                    else if (key == "video_bitrate")
                        user_config_video_bitrate = std::stoi(val);
                    else if (key == "force_width")
                        user_config_force_width = std::stoi(val);
                    else if (key == "force_height")
                        user_config_force_height = std::stoi(val);
                    else if (key == "force_fps")
                        user_config_force_fps = std::stoi(val);
                    else if (key == "disable_hw_encoding")
                        user_config_disable_hw_encoding = (val == "true" || val == "1");
                }
                catch (...)
                {
                    LOG_E("Invalid config value for key: " << key);
                }
            }
            break; // Stop after finding the first valid config file
        }
    }
}
