# ルール
- gitはリードオンリーコマンド除き使用してはならない。
- スクリプトを実行する場合はphpコマンドバージョン8.4を使用すること。イータープリンター経由で実行する場合、exitしなければ永遠に帰ってこないので注意すること。
- ファイルを書く場合、ideが提供する機能を使用して書け。応答に時間がかかるからという理由で、コマンド書き込みに切り替えるのは無し。
- $content.Replaceで書き換えるのはエンコーディングがぶっ壊れるので基本無し。
- 
# uiデザイン
- .interface-design/.claude/commands/init.md
- .interface-design/.claude/skills/interface-design/SKILL.md
を読むこと。

* Read this file at the start of every chat.
* Only solve the requested problem. Do not do extra work.
* However, for creative tasks, it is acceptable to implement features other than those requested.
* All file writes must use `apply_patch`.
* Never use command-based replacement. Use partial edits only.
* Do not delete existing comments.
* Do not read large files without a clear reason.
* Limit searches to the smallest relevant path.
* If you're unsure whether a file is worth reading, read only the first 100~280 lines and then decide whether you should read further.
* When using `apply_patch`, you should avoid deleting all lines and then rewriting the entire file with the same content whenever possible. This does not mean you should not replace the file.
* If you write Japanese with` apply_patch`, the characters will not be garbled. Garbled characters are a Powershell problem. If the characters are garbled, we will notify you so you can complete the task with confidence.
* Don't translate everything into English just because the output of Get-Content is garbled. As long as you use apply_patch, you won't have garbled characters.
* [Console]::InputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; Get-Content -Encoding UTF8 file.txt
* [Console]::InputEncoding = [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; $i=1; Get-Content -Encoding UTF8 file.txt | % { "$i: $_"; $i++ }
    * This project avoids circular references as much as possible
* You don't need to submit me the line numbers you edited. I don't need to reread the file in the final submission just to know the line numbers. Because of Git, the filename alone is sufficient.
* rg command installed
* Minimize the number of differences during editing. If this is not possible, break it down into smaller parts.
* Please submit the differences one file at a time, if possible.
* It's a waste of tokens, so you don't need to check the entire diff with git diff and submit the line numbers to me.


When modifying the UI, read interface-design-main/.claude/skills/interface-design/SKILL.md.
A UI description is not a specification report. When adding a description, you should write about what the user should expect and what they should input, rather than just saying "it's based on xx" or "it uses performance.now()".
In a UI description, you need to explain what it is, what happens as a result, and what the user needs to do.

## mcp server

Use these tools if you feel it's necessary. Of course, it's perfectly fine to complete everything using only commands.
In particular, `get_symbol_info` should be superior to a Blue Force-style search.

execute_run_configuration
get_run_configurations
get_file_problems
get_project_dependencies
get_project_modules
create_new_file
find_files_by_glob
find_files_by_name_keyword
get_all_open_file_paths
list_directory_tree
open_file_in_editor
reformat_file
get_file_text_by_path
replace_text_in_file
search_in_files_by_regex
get_symbol_info
rename_refactoring
execute_terminal_command
get_repositories

# build command
cmake -S . -B cmake-build-release2 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release2 --target rbe_lv15_sp22_v2 --config Release

# agents.md End
If the context has been injected up to this point, do not reread AGENTS.md file
