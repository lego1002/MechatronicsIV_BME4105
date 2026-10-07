# Git / GitHub 新手教學

不熟 Git 也沒關係：**照著第 1 節做一次設定，之後每天照第 3 節的流程就好**。
需要的操作都包在 `git_helper.sh` 這個選單工具裡，不用背指令。

## 0. 先搞懂四個名詞

| 名詞 | 白話解釋 |
|---|---|
| **repo**（儲存庫） | 整個專案資料夾，包含所有版本紀錄 |
| **commit**（提交） | 幫目前的程式拍一張「存檔快照」，可以隨時回到這個版本 |
| **branch**（分支） | 平行的工作線。每個人在自己的分支開發，不會弄壞別人的程式 |
| **push / pull** | push 把你的 commit 上傳到 GitHub；pull 把別人的更新下載回來 |

```
main      ●───────●───────────●───   ← 永遠保持「可以執行」
           \             ↗ (Pull Request 合併)
feature/x   ●───●───●───┘            ← 你在這裡開發
```

## 1. 第一次設定（每台電腦做一次）

1. 到 <https://github.com> 註冊帳號，然後請組長把你加入專案（Settings → Collaborators）
2. 安裝 Git：
   - **Windows**：到 <https://git-scm.com> 下載安裝，全部按預設即可。之後都用開始選單裡的 **Git Bash** 操作
   - **Mac**：打開終端機輸入 `git --version`，照提示安裝
   - **Ubuntu / Pi**：`sudo apt install git`
3. 打開終端機（Windows 用 Git Bash），下載這個小工具並執行：

   ```bash
   curl -O https://raw.githubusercontent.com/<帳號>/<repo>/main/docs/github_tutorial/git_helper.sh
   bash git_helper.sh
   ```
4. 選 **0**：輸入名字和 Email，工具會產生 SSH 金鑰。照畫面指示把金鑰貼到 GitHub
5. 選 **1**：貼上專案網址，下載專案
6. 之後都在專案資料夾裡執行：`bash docs/github_tutorial/git_helper.sh`

## 2. 每天的工作流程

```
開始寫之前 → 3. 更新
新的功能   → 4. 開新分支（例如 feature/arduino-motor）
寫程式 ...
告一段落   → 6. 儲存修改（commit，可以存很多次）
做完了     → 7. 上傳（push）→ 打開它給的網址 → 發 Pull Request
隊友看過   → 在 GitHub 按 Merge → 回到第一步
```

## 3. 選單對照表

| 選項 | 做什麼 | 對應的 Git 指令 |
|---|---|---|
| 0 | 第一次設定 | `git config`、`ssh-keygen` |
| 1 | 下載專案 | `git clone <網址>` |
| 2 | 看目前狀態 | `git status` |
| 3 | 從 GitHub 更新 | `git pull` |
| 4 | 開新分支 | `git switch -c <分支名>` |
| 5 | 切換分支 | `git switch <分支名>` |
| 6 | 儲存修改 | `git add -A` + `git commit -m "訊息"` |
| 7 | 上傳 | `git push -u origin <分支名>` |
| 8 | 合併最新的 main | `git fetch` + `git merge origin/main` |
| 9 | 歷史紀錄 | `git log --oneline --graph` |

工具執行時會把實際的指令印出來（藍色 `$` 開頭），用久了自然就學會了。

## 4. 團隊規則

1. **不要直接改 `main`**。所有修改都要經過分支 → Pull Request → 合併（工具會擋住在 main 上 commit 或 push）
2. **分支命名**：`feature/功能`、`fix/問題`、`docs/文件`
3. **commit 訊息**：`類型: 做了什麼`，例如 `feat: 新增超音波讀取`、`fix: 修正馬達反轉`
4. **小步提交**：一個 commit 只做一件事，比一次改一大堆更容易找出問題
5. **不要上傳大檔案**：影片、簡報、執行檔不要放進 repo（GitHub 單檔上限 100MB，`.gitignore` 已經幫你擋掉常見的格式）

## 5. 常見問題

**Q：出現 conflict（衝突）怎麼辦？**
代表你和隊友改到同一行。打開工具列出的檔案，會看到：
```
<<<<<<< HEAD
你的版本
=======
別人的版本
>>>>>>> origin/main
```
保留正確的內容、刪掉這三行標記，再選 **6** 儲存。不會處理的話，可以先執行 `git merge --abort` 取消合併，再找組長幫忙。

**Q：push 時出現 `rejected`？**
代表 GitHub 上有你沒有的更新。先選 **3** 更新，再選 **7** 上傳。

**Q：push 時出現 `Permission denied (publickey)`？**
SSH 金鑰沒有設定好，重新選 **0**，確認金鑰有貼到 GitHub。

**Q：commit 錯了想反悔？**
還沒 push 的話，執行 `git reset --soft HEAD~1`，可以撤銷上一次 commit，修改內容會保留。已經 push 的話，請找組長處理。

**Q：Windows 改過的 `.sh` 檔在 Pi 上不能執行？**
這是換行字元的問題，`.gitattributes` 已經設定好會自動轉換。如果還是有問題，在 Pi 上執行 `sed -i 's/\r$//' 檔名.sh`。
