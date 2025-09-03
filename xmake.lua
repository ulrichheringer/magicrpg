add_rules("mode.debug", "mode.release")

target("magicrpg")
    set_kind("binary")
    add_files("main.c")
    set_languages("c23")
    add_links("allegro_primitives", "allegro_font", "allegro_ttf", "allegro_image", "allegro", "sqlite3")
    add_syslinks("m", "pthread", "dl")
    if is_mode("debug") then
        set_targetdir("build/debug")
    else
        set_targetdir("build/release")
    end

    after_build(function (target)
        if os.isdir("assets") then
            print("Copying assets to output directory...")
            os.cp("assets", path.join(target:targetdir(), "assets"))
        end
        if os.isfile("pirulen.ttf") then
            os.cp("pirulen.ttf", target:targetdir())
        end
        if os.isfile("questions.db") then
            print("Copying questions.db to output directory...")
            os.cp("questions.db", target:targetdir())
        end
    end)


target("db_creator")
    set_kind("binary")
    add_files("db_creator.c")
    add_links("sqlite3")
    add_syslinks("m", "pthread", "dl")
    if is_mode("debug") then
        set_targetdir("build/debug")
    else
        set_targetdir("build/release")
    end

