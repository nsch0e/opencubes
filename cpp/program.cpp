#include <iostream>

#include "cmdparser.hpp"
#include "config.hpp"
#include "cubes.hpp"

void configure_arguments(cli::Parser& parser) {
    parser.set_optional<int>("n", "cube_size", 1, "the size of polycube to generate up to");
    parser.set_optional<int>("t", "threads", 1, "the number of threads to use while generating");
    parser.set_optional<bool>("c", "use_cache", false, "whether to load cache files");
    parser.set_optional<bool>("w", "write_cache", false, "wheather to save cache files");
    parser.set_optional<bool>("s", "split_cache", false, "wheather to save in sparate cache files per output shape");
    parser.set_optional<bool>("v", "version", false, "print build version info");
    parser.set_optional<bool>("u", "use_split_cache", false, "use separate cachefile by input shape");
    parser.set_optional<std::string>("f", "cache_file_folder", "./cache/", "where to store cache files");
}

int main(int argc, char** argv) {
    cli::Parser parser(argc, argv);
    configure_arguments(parser);
    parser.run_and_exit_if_error();
    if (parser.get<bool>("v")) {
        std::printf("Built from %s, %s, %s\n", CONFIG_VERSION, CONFIG_BUILDTYPE, CONFIG_COMPILERID);
    }
    gen(parser.get<int>("n"), parser.get<int>("t"), parser.get<bool>("c"), parser.get<bool>("w"), parser.get<bool>("s"), parser.get<bool>("u"), parser.get<std::string>("f"));
    return 0;
}
