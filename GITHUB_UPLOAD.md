# Aggiornare Easy-Chat su GitHub

Repository locale:

```bash
~/Scaricati/Easy-Chat-local
```

Per applicare la v0.7.0 sopra il clone Git esistente:

```bash
unzip -o ~/Scaricati/EChat-GitHub-ready-v0.7.0.zip -d ~/Scaricati/Easy-Chat-local
cd ~/Scaricati/Easy-Chat-local

git status
git add -A
git commit -m "EChat v0.7 dual Light and Full UI"
git push
```

Il push su `main` avvia `Build and Release EChat`. Se Windows e Linux sono verdi, viene pubblicata la prerelease `v0.7.0`.
