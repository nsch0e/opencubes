#include <iostream>

#include "cmdparser.hpp"
#include "cubes.hpp"

void configure_arguments(cli::Parser& parser) {
    parser.set_required<int>("n", "cube_size", "the size of polycube to generate up to");
    parser.set_optional<int>("t", "threads", 1, "the number of threads to use while generating");
    parser.set_optional<bool>("c", "use_cache", false, "whether to load cache files");
    parser.set_optional<bool>("w", "write_cache", false, "wheather to save cache files");
    parser.set_optional<bool>("s", "split_cache", false, "wheather to save in sparate cache files per output shape");
    parser.set_optional<bool>("u", "use_split_cache", false, "use separate cachefile by input shape");
}

int main(int argc, char** argv) {
    cli::Parser parser(argc, argv);
    configure_arguments(parser);
    parser.run_and_exit_if_error();
    gen(parser.get<int>("n"), parser.get<int>("t"), parser.get<bool>("c"), parser.get<bool>("w"), parser.get<bool>("s"), parser.get<bool>("u"));
    return 0;
}
