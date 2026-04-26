# ルール
- gitはリードオンリーコマンド除き使用してはならない。
- rgコマンドは必ず失敗する。
- スクリプトを実行する場合はphpコマンドバージョン8.4を使用すること。イータープリンター経由で実行する場合、exitしなければ永遠に帰ってこないので注意すること。
- ファイルを書く場合、ideが提供する機能を使用して書け。応答に時間がかかるからという理由で、コマンド書き込みに切り替えるのは無し。
- $content.Replaceで書き換えるのはエンコーディングがぶっ壊れるので基本無し。
- 
# uiデザイン
- .interface-design/.claude/commands/init.md
- .interface-design/.claude/skills/interface-design/SKILL.md
を読むこと。

# フロントエンドコード
- publicから探すこと。

public/app.js
public/index.html

# テーマ
- フロントエンドの新uiは、テーマに沿うこと

# 言語設定
- public/index.htmlの言語コンフィグは追加時に適切に変更すること。

