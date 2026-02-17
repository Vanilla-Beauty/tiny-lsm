-- 定义项目
set_project("tiny-lsm")
set_version("0.0.1")
set_languages("c++20")

add_rules("mode.debug", "mode.release", "mode.coverage")

-- 在 coverage 模式下设置 flags
if is_mode("coverage") then
    add_cxxflags("--coverage")
    add_ldflags("--coverage")
end

add_repositories("local-repo build")

add_requires("gtest")
add_requires("asio")
add_requires("pybind11")
add_requires("spdlog", { system = false })
add_requires("toml11", { system = false })

if is_mode("debug") then
    add_defines("LSM_DEBUG")
end

target("logger")
    set_kind("static")
    add_files("src/logger/*.cpp")
    add_packages("spdlog")
    add_includedirs("include", {public = true})
    
target("config")
    set_kind("static")
    add_files("src/config/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("utils")
    set_kind("static")
    add_files("src/utils/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("iterator")
    set_kind("static")
    add_files("src/iterator/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("skiplist")
    set_kind("static")
    add_files("src/skiplist/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("memtable")
    set_kind("static")
    add_deps("skiplist", "iterator", "config", "sst")
    add_packages("toml11", "spdlog")
    add_files("src/memtable/*.cpp")
    add_includedirs("include", {public = true})

target("block")
    set_kind("static")
    add_deps("config")
    add_files("src/block/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("sst")
    set_kind("static")
    add_deps("block", "utils", "iterator")
    add_files("src/sst/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("wal")
    set_kind("static")
    add_deps("sst", "memtable")
    add_files("src/wal/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("lsm")
    set_kind("static")
    add_deps("sst", "memtable", "wal", "logger")
    add_files("src/lsm/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("redis")
    set_kind("static")
    add_deps("lsm")
    add_files("src/redis_wrapper/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

-- ============ 共享库目标（供外部使用） ============

target("lsm_shared")
    set_kind("shared")
    add_files("src/logger/*.cpp", "src/config/*.cpp", "src/utils/*.cpp", 
              "src/iterator/*.cpp", "src/skiplist/*.cpp", "src/memtable/*.cpp",
              "src/block/*.cpp", "src/sst/*.cpp", "src/wal/*.cpp", "src/lsm/*.cpp",
              "src/redis_wrapper/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})  -- 确保包含路径正确
    set_targetdir("$(builddir)/lib")

    if is_plat("windows") then
        set_extension(".dll")
        add_defines("TINYLSM_EXPORTS")
        add_cxxflags("/LD")
    else
        set_extension(".so")
    end

    on_install(function (target)
        os.cp("include", path.join(target:installdir(), "include/tiny-lsm"))
        local libfile = target:targetfile()
        if is_plat("windows") then
            os.cp(libfile, path.join(target:installdir(), "bin"))
            local implib = path.join(path.directory(libfile), target:name() .. ".lib")
            if os.isfile(implib) then
                os.cp(implib, path.join(target:installdir(), "lib"))
            end
        else
            os.cp(libfile, path.join(target:installdir(), "lib"))
        end
    end)

-- ============ 测试目标 ============

target("test_config")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_config.cpp")
    add_deps("logger", "config")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_skiplist")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_skiplist.cpp")
    add_deps("logger", "skiplist")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_memtable")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_memtable.cpp")
    add_deps("logger", "memtable")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_block")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block.cpp")
    add_deps("logger", "block")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_blockmeta")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_blockmeta.cpp")
    add_deps("logger", "block")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_utils")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_utils.cpp")
    add_deps("logger", "utils")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_sst")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_sst.cpp")
    add_deps("logger", "sst")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_lsm")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_lsm.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_block_cache")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block_cache.cpp")
    add_deps("logger", "block")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

target("test_compact")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_compact.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")
    add_packages("gtest", "toml11", "spdlog")
    add_includedirs("include", {public = true})

target("test_redis")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_redis.cpp")
    add_deps("logger", "redis", "memtable", "iterator")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

target("test_wal")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_wal.cpp")
    add_deps("logger", "wal", "lsm")
    add_includedirs("include", {public = true})
    add_packages("gtest", "toml11", "spdlog")

-- ============ 可执行目标 ============

target("example")
    set_kind("binary")
    add_files("example/main.cpp")
    add_deps("logger", "config", "utils", "iterator", "skiplist", 
             "memtable", "block", "sst", "wal", "lsm", "redis")
    add_includedirs("include")  -- 显式添加包含路径
    set_targetdir("$(builddir)/bin")

target("debug")
    set_kind("binary")
    add_files("example/debug.cpp")
    add_deps("logger", "config", "utils", "iterator", "skiplist", 
             "memtable", "block", "sst", "wal", "lsm", "redis")
    add_includedirs("include")  -- 显式添加包含路径
    set_targetdir("$(builddir)/bin")

target("server")
    set_kind("binary")
    add_files("server/src/*.cpp")
    add_deps("redis")
    add_includedirs("include", {public = true})
    add_packages("asio")
    set_targetdir("$(builddir)/bin")

-- ============ Python 绑定 ============

-- 根据平台选择合适的lsm_pybind目标
if is_plat("windows") then
    target("lsm_pybind")
        set_kind("shared")
        add_files("sdk/lsm_pybind.cpp")
        add_packages("pybind11")
        add_deps("lsm")  -- Windows下使用原来的依赖
        add_includedirs("include", {public = true})
        set_targetdir("$(builddir)/lib")
        set_filename("lsm_pybind.pyd")
        add_cxxflags("/LD")
else
    -- Unix/Linux/macOS平台
    target("lsm_pybind")
        set_kind("shared")
        add_files("sdk/lsm_pybind.cpp")
        add_packages("pybind11")
        add_deps("lsm_shared")  -- Unix下使用共享库依赖
        add_includedirs("include", {public = true})
        set_targetdir("$(builddir)/lib")
        set_filename("lsm_pybind.so")
        add_ldflags("-Wl,-rpath,$ORIGIN")
        add_defines("TINYLSM_EXPORT=__attribute__((visibility(\"default\")))")
        add_cxxflags("-fvisibility=hidden")
end

-- ============ 测试任务 ============

task("run-all-tests")
    set_category("plugin")
    set_menu {
        usage = "xmake run-all-tests",
        description = "Build and run all test binaries"
    }

    on_run(function ()
        import("core.project.project")

        local targets = project.targets()
        local test_targets = {}

        for name, _ in pairs(targets) do
            if name:startswith("test_") then
                table.insert(test_targets, name)
            end
        end

        table.sort(test_targets)

        if #test_targets == 0 then
            print("\27[33m[Warning] No test targets found.\27[0m")
            return
        end

        for _, name in ipairs(test_targets) do
            print("\27[32m>> Running\27[0m " .. name)
            os.execv("xmake", {"run", name})
            print("")
        end

        print("\27[32mAll tests finished.\27[0m")
    end)