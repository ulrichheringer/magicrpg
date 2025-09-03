{
    values = {
        "/usr/bin/gcc",
        {
            "-m64",
            "-fvisibility=hidden",
            "-O3",
            "-std=c23",
            "-DNDEBUG"
        }
    },
    depfiles_format = "gcc",
    depfiles = "main.o: main.c\
",
    files = {
        "main.c"
    }
}