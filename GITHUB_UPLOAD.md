# Upload iniziale su GitHub

Da Fedora, dalla directory contenente i file del progetto:

```bash
git init
git branch -M main
git add .
git commit -m "EChat v0.2"
git remote add origin https://github.com/CosmoUniverso/Easy-Chat.git
git push -u origin main
```

Se GitHub richiede autenticazione, usa GitHub CLI (`gh auth login`) o una credenziale/token GitHub; la password dell'account GitHub non viene accettata come password Git HTTPS.

Dopo il push, apri la scheda **Actions** del repository. Il workflow `Build and Release EChat` compilerà Windows e Linux. Se entrambi i job terminano correttamente, la scheda **Releases** conterrà:

- `EChat-Windows-x64-Setup.exe`
- `EChat-Linux-x86_64.AppImage`
