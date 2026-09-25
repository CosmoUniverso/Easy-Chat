# Aggiornare Easy-Chat su GitHub

Repository locale:

```bash
~/Scaricati/Easy-Chat-local
```

Per applicare la patch v0.6.0:

```bash
unzip -o ~/Scaricati/EChat-GitHub-ready-v0.6.0.zip -d ~/Scaricati/Easy-Chat-local
cd ~/Scaricati/Easy-Chat-local

git status
git add -A
git commit -m "EChat v0.6 compact stealth mode"
git push
```

Il push su `main` avvia `Build and Release EChat`. Se Windows e Linux sono verdi, viene pubblicata la prerelease `v0.6.0`.
