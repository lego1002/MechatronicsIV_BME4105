#!/bin/bash
# Git 小幫手：用選單完成常用的 Git 操作
# 用法：在 repo 資料夾裡執行  bash docs/github_tutorial/git_helper.sh
# Windows 請用「Git Bash」執行

MAIN=main

green()  { printf '\033[32m%s\033[0m\n' "$*"; }
yellow() { printf '\033[33m%s\033[0m\n' "$*"; }
red()    { printf '\033[31m%s\033[0m\n' "$*"; }
run()    { printf '\033[36m$ %s\033[0m\n' "$*"; "$@"; }   # 把執行的指令印出來，順便學指令

in_repo()    { git rev-parse --is-inside-work-tree >/dev/null 2>&1; }
branch()     { git rev-parse --abbrev-ref HEAD 2>/dev/null; }
need_repo()  { in_repo || { red "目前不在 repo 資料夾裡，請先 cd 進專案資料夾（或選 1 下載專案）"; return 1; }; }
has_changes(){ [ -n "$(git status --porcelain)" ]; }

confirm() {
  read -r -p "$1 [y/N] " ans
  [ "$ans" = "y" ] || [ "$ans" = "Y" ]
}

# ---------------------------------------------------------------- 0. 第一次設定
setup() {
  echo "== 第一次設定（每台電腦只要做一次）=="
  name=$(git config --global user.name)
  email=$(git config --global user.email)
  read -r -p "你的名字（英文，顯示在 commit 上）[${name}]: " n
  read -r -p "你的 Email（要和 GitHub 帳號相同）[${email}]: " e
  [ -n "$n" ] && run git config --global user.name "$n"
  [ -n "$e" ] && run git config --global user.email "$e"
  run git config --global init.defaultBranch main
  run git config --global pull.rebase false

  key="$HOME/.ssh/id_ed25519"
  if [ ! -f "$key.pub" ]; then
    echo; yellow "建立 SSH 金鑰（之後 push 就不用一直輸入密碼）"
    run ssh-keygen -t ed25519 -C "$(git config --global user.email)" -f "$key" -N ""
  fi
  echo; green "請複製下面這一整行："
  cat "$key.pub"
  echo
  echo "然後到 GitHub → 右上角頭像 → Settings → SSH and GPG keys → New SSH key → 貼上 → Add"
  read -r -p "貼好之後按 Enter 測試連線..." _
  ssh -T -o StrictHostKeyChecking=accept-new git@github.com
  [ $? -eq 1 ] && green "連線成功！（看到 successfully authenticated 就代表成功）"
}

# ---------------------------------------------------------------- 1. 下載專案
clone() {
  echo "== 下載專案 =="
  echo "網址在 GitHub 專案頁面 → 綠色 Code 按鈕 → SSH 分頁，例如 git@github.com:帳號/vacuum.git"
  read -r -p "專案網址: " url
  [ -z "$url" ] && return
  run git clone "$url" && green "完成！請執行 cd $(basename "$url" .git) 進入資料夾"
}

# ---------------------------------------------------------------- 2. 看狀態
status() {
  need_repo || return
  echo "目前分支：$(green "$(branch)")"
  run git status --short --branch
  has_changes || green "沒有未儲存的修改"
}

# ---------------------------------------------------------------- 3. 更新
update() {
  need_repo || return
  b=$(branch)
  if has_changes; then
    yellow "你有還沒 commit 的修改，請先「6. 儲存修改」再更新"; return
  fi
  run git fetch origin
  if [ "$b" = "$MAIN" ]; then
    run git pull origin "$MAIN"
  elif git rev-parse --verify -q "origin/$b" >/dev/null; then
    run git pull origin "$b"
  else
    yellow "分支 $b 還沒推上 GitHub，不用更新"
  fi
}

# ---------------------------------------------------------------- 4. 開新分支
new_branch() {
  need_repo || return
  if has_changes; then
    yellow "你有還沒 commit 的修改。"
    confirm "要把這些修改一起帶到新分支嗎？" || return
    carry=1
  fi
  echo "分支命名建議：feature/做什麼、fix/修什麼、docs/寫什麼"
  echo "例如：feature/arduino-motor、fix/stream-lag"
  read -r -p "新分支名稱: " nb
  [ -z "$nb" ] && return
  run git fetch origin
  if [ -n "$carry" ]; then
    run git switch -c "$nb"
  else
    run git switch -c "$nb" "origin/$MAIN" --no-track
  fi
  green "已切換到新分支 $nb，開始寫程式吧"
}

# ---------------------------------------------------------------- 5. 切換分支
switch_branch() {
  need_repo || return
  if has_changes; then
    yellow "你有還沒 commit 的修改，請先「6. 儲存修改」再切換"; return
  fi
  run git fetch origin --prune
  echo "本機分支："; git branch
  echo "GitHub 上的分支："; git branch -r | grep -v HEAD
  read -r -p "要切換到哪個分支（不用打 origin/）: " b
  [ -n "$b" ] && run git switch "$b"
}

# ---------------------------------------------------------------- 6. 儲存修改
save() {
  need_repo || return
  if ! has_changes; then green "沒有需要儲存的修改"; return; fi
  if [ "$(branch)" = "$MAIN" ]; then
    red "你現在在 $MAIN 分支上！請先用「4. 開新分支」（修改會一起帶過去）"; return
  fi
  run git status --short
  echo
  echo "commit 訊息建議格式：類型: 做了什麼"
  echo "  feat: 新增馬達控制    fix: 修正超音波讀值    docs: 更新 README"
  read -r -p "這次改了什麼: " msg
  [ -z "$msg" ] && { yellow "訊息不能空白"; return; }
  run git add -A
  run git commit -m "$msg" && green "已儲存（只存在你的電腦，記得「7. 上傳」）"
}

# ---------------------------------------------------------------- 7. 上傳
push() {
  need_repo || return
  b=$(branch)
  if [ "$b" = "$MAIN" ]; then
    red "不要直接上傳到 $MAIN！請開分支，再用 Pull Request 合併"; return
  fi
  has_changes && yellow "提醒：還有修改沒 commit，這些不會被上傳"
  run git push -u origin "$b" || return
  url=$(git remote get-url origin | sed -E 's#git@github.com:#https://github.com/#; s#\.git$##')
  green "上傳成功！到下面網址開 Pull Request，請隊友幫忙看過再合併："
  echo "$url/compare/$MAIN...$b?expand=1"
}

# ---------------------------------------------------------------- 8. 合併最新的 main
sync_main() {
  need_repo || return
  b=$(branch)
  [ "$b" = "$MAIN" ] && { update; return; }
  if has_changes; then
    yellow "你有還沒 commit 的修改，請先「6. 儲存修改」"; return
  fi
  run git fetch origin
  if run git merge "origin/$MAIN"; then
    green "已把最新的 $MAIN 合併進 $b"
  else
    red "發生衝突（conflict）！"
    echo "1. 打開下面這些檔案，找到 <<<<<<< ======= >>>>>>> 標記"
    git diff --name-only --diff-filter=U
    echo "2. 保留正確的內容，刪掉標記"
    echo "3. 改完後再執行「6. 儲存修改」"
    echo "（不想處理的話，執行 git merge --abort 就能回到合併前）"
  fi
}

# ---------------------------------------------------------------- 9. 歷史紀錄
history() {
  need_repo || return
  run git log --oneline --graph --decorate -15
}

# ---------------------------------------------------------------- 主選單
while true; do
  echo
  echo "========== Git 小幫手 =========="
  in_repo && echo "目前分支：$(branch)"
  echo " 0. 第一次設定（名字、Email、SSH 金鑰）"
  echo " 1. 下載專案（clone）"
  echo " 2. 看目前狀態（status）"
  echo " 3. 從 GitHub 更新（pull）"
  echo " 4. 開新分支（branch）"
  echo " 5. 切換分支（switch）"
  echo " 6. 儲存修改（add + commit）"
  echo " 7. 上傳到 GitHub（push）"
  echo " 8. 合併最新的 main 到我的分支（merge）"
  echo " 9. 看歷史紀錄（log）"
  echo " q. 離開"
  read -r -p "請選擇: " c
  case "$c" in
    0) setup ;; 1) clone ;; 2) status ;; 3) update ;; 4) new_branch ;;
    5) switch_branch ;; 6) save ;; 7) push ;; 8) sync_main ;; 9) history ;;
    q|Q) exit 0 ;;
    *) yellow "請輸入選單上的數字" ;;
  esac
done
