# Aggiornare Easy-Chat su GitHub

Repository locale usato durante lo sviluppo:

```bash
~/Scaricati/Easy-Chat-local
```

Per applicare lo ZIP v0.3:

```bash
unzip -o ~/Scaricati/EChat-GitHub-ready-v0.3.zip -d ~/Scaricati/Easy-Chat-local
cd ~/Scaricati/Easy-Chat-local

git status
git add -A
git commit -m "EChat v0.3 mesh QUIC and TCP fallback"
git push
```

Il push su `main` avvia `Build and Release EChat`.

Se i job Windows e Linux sono verdi, la release `v0.3.0` conterrà:

- `EChat-Windows-x64-Setup.exe`
- `EChat-Linux-x86_64.AppImage`

Se una build diventa rossa, apri il job fallito in **Actions**: il log compiler/package è la fonte da usare per la correzione successiva.
