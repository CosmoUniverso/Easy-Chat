# Aggiornare Easy-Chat su GitHub

Repository locale usato durante lo sviluppo:

```bash
~/Scaricati/Easy-Chat-local
```

Per applicare lo ZIP v0.5.0:

```bash
unzip -o ~/Scaricati/EChat-GitHub-ready-v0.5.0.zip -d ~/Scaricati/Easy-Chat-local
cd ~/Scaricati/Easy-Chat-local

git status
git add -A
git commit -m "EChat v0.5 message deletion group management and account settings"
git push
```

Il push su `main` avvia `Build and Release EChat`.

Se i job Windows e Linux sono verdi, la release `v0.5.0` conterrà:

- `EChat-Windows-x64-Setup.exe`
- `EChat-Linux-x86_64.AppImage`

Se una build diventa rossa, il log del job fallito in **Actions** è la fonte da usare per la correzione successiva.
