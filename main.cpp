#include <cassert>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using filesystem::path;

path operator""_p(const char* data, std::size_t sz) {
    return path(data, data + sz);
}

// напишите эту функцию
bool Preprocess(const path& in_file, const path& out_file, const vector<path>& include_directories) {
    ifstream in_stream(in_file);
    if (!in_stream.is_open()) {
        return false;
    }

    ofstream out_stream(out_file);
    if (!out_stream.is_open()) {
        return false;
    }

    static const regex include_quotes("\\s*#\\s*include\\s*\"([^\"]*)\"\\s*");
    static const regex include_angles("\\s*#\\s*include\\s*<([^>]*)>\\s*");

    function<bool(istream&, ostream&, const path&, const vector<path>&)> process_file =
        [&](istream& input, ostream& output, const path& current_file, const vector<path>& include_dirs) -> bool {

        string line;
        int line_number = 0;

        while (getline(input, line)) {
            line_number++;

            smatch match;

            if (regex_match(line, match, include_quotes)) {
                path include_path = string(match[1]);
                path full_path = current_file.parent_path() / include_path;

                ifstream include_file(full_path);
                if (include_file.is_open()) {
                    if (!process_file(include_file, output, full_path, include_dirs)) {
                        return false;
                    }
                    continue;
                }

                bool found = false;
                for (const auto& include_dir : include_dirs) {
                    full_path = include_dir / include_path;
                    include_file.open(full_path);
                    if (include_file.is_open()) {
                        if (!process_file(include_file, output, full_path, include_dirs)) {
                            return false;
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    cout << "unknown include file " << include_path.string()
                        << " at file " << current_file.string()
                        << " at line " << line_number << endl;
                    return false;
                }
            }
            else if (regex_match(line, match, include_angles)) {
                path include_path = string(match[1]);
                bool found = false;

                for (const auto& include_dir : include_dirs) {
                    path full_path = include_dir / include_path;
                    ifstream include_file(full_path);
                    if (include_file.is_open()) {
                        if (!process_file(include_file, output, full_path, include_dirs)) {
                            return false;
                        }
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    cout << "unknown include file " << include_path.string()
                        << " at file " << current_file.string()
                        << " at line " << line_number << endl;
                    return false;
                }
            }
            else {
                output << line << endl;
            }
        }

        return true;
        };

    return process_file(in_stream, out_stream, in_file, include_directories);
}

string GetFileContents(string file) {
    ifstream stream(file);

    // конструируем string по двум итераторам
    return { (istreambuf_iterator<char>(stream)), istreambuf_iterator<char>() };
}

void Test() {
    error_code err;
    filesystem::remove_all("sources"_p, err);
    filesystem::create_directories("sources"_p / "include2"_p / "lib"_p, err);
    filesystem::create_directories("sources"_p / "include1"_p, err);
    filesystem::create_directories("sources"_p / "dir1"_p / "subdir"_p, err);

    {
        ofstream file("sources/a.cpp");
        file << "// this comment before include\n"
            "#include \"dir1/b.h\"\n"
            "// text between b.h and c.h\n"
            "#include \"dir1/d.h\"\n"
            "\n"
            "int SayHello() {\n"
            "    cout << \"hello, world!\" << endl;\n"
            "#   include<dummy.txt>\n"
            "}\n"s;
    }
    {
        ofstream file("sources/dir1/b.h");
        file << "// text from b.h before include\n"
            "#include \"subdir/c.h\"\n"
            "// text from b.h after include"s;
    }
    {
        ofstream file("sources/dir1/subdir/c.h");
        file << "// text from c.h before include\n"
            "#include <std1.h>\n"
            "// text from c.h after include\n"s;
    }
    {
        ofstream file("sources/dir1/d.h");
        file << "// text from d.h before include\n"
            "#include \"lib/std2.h\"\n"
            "// text from d.h after include\n"s;
    }
    {
        ofstream file("sources/include1/std1.h");
        file << "// std1\n"s;
    }
    {
        ofstream file("sources/include2/lib/std2.h");
        file << "// std2\n"s;
    }

    bool result = Preprocess("sources"_p / "a.cpp"_p, "sources"_p / "a.in"_p,
        { "sources"_p / "include1"_p,"sources"_p / "include2"_p });

    // Функция должна вернуть false из-за ошибки с dummy.txt
    assert(!result);

    // Проверяем, что в выходном файле есть содержимое до ошибки
    ostringstream test_out;
    test_out << "// this comment before include\n"
        "// text from b.h before include\n"
        "// text from c.h before include\n"
        "// std1\n"
        "// text from c.h after include\n"
        "// text from b.h after include\n"
        "// text between b.h and c.h\n"
        "// text from d.h before include\n"
        "// std2\n"
        "// text from d.h after include\n"
        "\n"
        "int SayHello() {\n"
        "    cout << \"hello, world!\" << endl;\n"s;

    assert(GetFileContents("sources/a.in"s) == test_out.str());

    // Тест для случая без ошибок
    {
        ofstream file("sources/test.cpp");
        file << "// test comment\n"
            "#include \"dir1/b.h\"\n"
            "// end of file\n"s;
    }

    bool success_result = Preprocess("sources"_p / "test.cpp"_p, "sources"_p / "test.out"_p,
        { "sources"_p / "include1"_p,"sources"_p / "include2"_p });

    // Функция должна вернуть true для корректного файла
    assert(success_result);

    ostringstream success_out;
    success_out << "// test comment\n"
        "// text from b.h before include\n"
        "// text from c.h before include\n"
        "// std1\n"
        "// text from c.h after include\n"
        "// text from b.h after include\n"
        "// end of file\n"s;

    assert(GetFileContents("sources/test.out"s) == success_out.str());
}

int main() {
    Test();
}