{
    depfiles = "db_creator.o: db_creator.c\
",
    depfiles_format = "gcc",
    values = {
        "/usr/bin/gcc",
        {
            "-m64",
            "-fvisibility=hidden",
            "-O3",
            "-DNDEBUG"
        }
    },
    files = {
        "db_creator.c"
    }
}