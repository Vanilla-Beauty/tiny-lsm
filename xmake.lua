-- 定义项目
set_project("toni-lsm")
set_version("0.0.1")
set_languages("c++20")

add_rules("mode.debug", "mode.release")

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
    set_kind("static")  -- 生成静态库
    add_files("src/logger/*.cpp")
    add_packages("spdlog")
    add_includedirs("include", {public = true})
    
target("config")
    set_kind("static")  -- 生成静态库
    add_files("src/config/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("utils")
    set_kind("static")  -- 生成静态库
    add_files("src/utils/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("iterator")
    set_kind("static")  -- 生成静态库
    add_files("src/iterator/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("skiplist")
    set_kind("static")  -- 生成静态库
    add_files("src/skiplist/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("memtable")
    set_kind("static")  -- 生成静态库
    add_deps("skiplist","iterator", "config")
    add_deps("sst")
    add_packages("toml11", "spdlog")
    add_files("src/memtable/*.cpp")
    add_includedirs("include", {public = true})

target("block")
    set_kind("static")  -- 生成静态库
    add_deps("config")
    add_files("src/block/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("sst")
    set_kind("static")  -- 生成静态库
    add_deps("block", "utils", "iterator")
    add_files("src/sst/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("wal")
    set_kind("static")  -- 生成静态库
    add_deps("sst", "memtable")
    add_files("src/wal/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("lsm")
    set_kind("static")  -- 生成静态库
    add_deps("sst", "memtable", "wal", "logger")
    add_files("src/lsm/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

target("redis")
    set_kind("static")  -- 生成静态库
    add_deps("lsm")
    add_files("src/redis_wrapper/*.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})

-- 定义动态链接库目标
target("lsm_shared")
    set_kind("shared")
    add_files("src/**.cpp")
    add_packages("toml11", "spdlog")
    add_includedirs("include", {public = true})
    set_targetdir("$(buildir)/lib")

    -- 安装头文件和动态链接库
    on_install(function (target)
        os.cp("include", path.join(target:installdir(), "include/toni-lsm"))
        os.cp(target:targetfile(), path.join(target:installdir(), "lib"))
    end)

-- 定义测试
target("test_config")
    set_kind("binary")  -- 生成可执行文件
    set_group("tests")
    add_files("test/test_config.cpp")
    add_deps("logger", "config")  -- 依赖skiplist库
    add_packages("gtest")  -- 添加gtest包
    add_packages("toml11", "spdlog")

target("test_skiplist")
    set_kind("binary")  -- 生成可执行文件
    set_group("tests")
    add_files("test/test_skiplist.cpp")
    add_deps("logger", "skiplist")  -- 依赖skiplist库
    add_packages("gtest")  -- 添加gtest包
    add_packages("toml11", "spdlog")

target("test_memtable")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_memtable.cpp")
    add_deps("logger", "memtable")  -- 如果memtable是独立的target，这里需要添加对应的依赖
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_block")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block.cpp")
    add_deps("logger", "block")  -- 如果memtable是独立的target，这里需要添加对应的依赖
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_blockmeta")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_blockmeta.cpp")
    add_deps("logger", "block")  -- 如果memtable是独立的target，这里需要添加对应的依赖
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_utils")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_utils.cpp")
    add_deps("logger", "utils")
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_sst")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_sst.cpp")
    add_deps("logger", "sst")
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_lsm")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_lsm.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")  -- Added memtable and iterator dependencies
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_block_cache")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_block_cache.cpp")
    add_deps("logger", "block")
    add_includedirs("include")
    add_packages("gtest")
    add_packages("toml11", "spdlog")

target("test_compact")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_compact.cpp")
    add_deps("logger", "lsm", "memtable", "iterator")  -- Added memtable and iterator dependencies
    add_packages("gtest")
    add_packages("toml11", "spdlog")
    add_includedirs("include")

target("test_redis")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_redis.cpp")
    add_deps("logger", "redis", "memtable", "iterator")  -- Added memtable and iterator dependencies
    add_includedirs("include")
    add_packages("gtest")
    add_packages("toml11", "spdlog")

target("test_wal")
    set_kind("binary")
    set_group("tests")
    add_files("test/test_wal.cpp")
    add_deps("logger", "wal")  -- Added memtable and iterator dependencies
    add_includedirs("include")
    add_packages("gtest")
    add_packages("toml11", "spdlog")

-- 定义 示例
target("example")
    set_kind("binary")
    add_files("example/main.cpp")
    add_deps("lsm_shared")
    add_includedirs("include", {public = true})
    set_targetdir("$(buildir)/bin")

-- 定义 debug 示例
target("debug")
    set_kind("binary")
    add_files("example/debug.cpp")
    add_deps("lsm_shared")
    add_includedirs("include", {public = true})
    set_targetdir("$(buildir)/bin")

-- 定义server
target("server")
    set_kind("binary")
    add_files("server/src/*.cpp")
    add_deps("redis")
    add_includedirs("include", {public = true})
    add_packages("asio")
    set_targetdir("$(buildir)/bin")

target("lsm_pybind")
    set_kind("shared")
    add_files("sdk/lsm_pybind.cpp")
    add_packages("pybind11")
    add_deps("lsm_shared")
    add_includedirs("include", {public = true})
    set_targetdir("$(buildir)/lib")
    set_filename("lsm_pybind.so")  -- 确保生成的文件名为 lsm_pybind.so
    add_ldflags("-Wl,-rpath,$ORIGIN")
    add_defines("TONILSM_EXPORT=__attribute__((visibility(\"default\")))")
    add_cxxflags("-fvisibility=hidden")  -- 隐藏非导出符号

task("run-all-tests")
    set_category("plugin")
    set_menu {
        usage = "xmake run-all-tests",
        description = "Build and run all test binaries (targets starting with 'test_')"
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
            -- print("\27[36m>> Building\27[0m " .. name)
            -- os.execv("xmake", {"build", name})

            print("\27[32m>> Running\27[0m " .. name)
            os.execv("xmake", {"run", name})
            print("")
        end

        print("\27[32mAll tests finished.\27[0m")
    end)


