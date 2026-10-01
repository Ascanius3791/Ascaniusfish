<!-- OWNERSHIP=Claude -->
# Relabel and retrain the net after an eval change (#56)

The net learns `label − static`. Once `eval()` changes, `static` changes too, so the
old net corrects an eval that no longer exists. Its gain fades until the dataset's
positions are labelled again and the net is retrained. The positions stay the same:
the 532,902 of the #50 dataset. Only their static eval, quiescence score and depth-6
label are computed again. A position that is no longer quiet, or whose label is now
a mate or table score, is dropped. Labels are searched with the net off.

Labelling takes about 7 CPU-hours here. A Claude cloud session does it in about
40 min (`docs/measurements/nne_relabel_cloud_2026-10-01.md`).

## Locally: one command

```
make nne-retrain                 # relabel, train, check
make nne-retrain MINUTES=60      # stop the relabel after an hour; the same command carries on
```

`tools/nne_retrain.sh` does three things:

1. **Relabel.** It runs `tools/nne_data relabel data/nne_fens.tsv.zst` into
   `data/nne-relabel/`. The FEN list is built from `data/nne/` if it is missing, or
   else fetched from branch `nne-fens`. This takes 85–105 min.
2. **Train.** It runs `tools/nne_train data=data/nne-relabel` into
   `nets/nne_d6.bin` and `nets/nne_d6_test.tsv`. The old pair is kept as
   `nets/*.prev.*`. This takes about 3.5 min on the GPU.
3. **Check.** It runs `make nne-test`'s check on the new net.

`tools/nne_train` has to be built on its own (`make tools/nne_train`, peak 1.25 GB).
Do that before the first run if nothing else may be compiling at the same time. Then
measure the new net as #53 did before relying on it.

## In a Claude cloud session

**Once.** The FEN list lives on the orphan branch `nne-fens`, as one file,
`nne_fens.tsv.zst` (14.9 MB). It holds game, index, ply and FEN of every position, in
index order: the first four columns of `data/nne/{train,valid,test}.tsv`. The file was
put there without a checkout:

```
blob=$(git hash-object -w data/nne_fens.tsv.zst)
tree=$(printf '100644 blob %s\tnne_fens.tsv.zst\n' "$blob" | git mktree)
git update-ref refs/heads/nne-fens "$(git commit-tree "$tree" -m 'nne: the dataset FEN list (#50)')"
git push origin nne-fens
```

The cloud environment needs network **Custom**, with `tablebase.lichess.ovh` added
to the default package managers. The WDL tables are downloaded from there (19 s); DTZ
is not needed. GitHub is reached through the session's git proxy.

**Each relabel**

1. The cloud session labels with the code it starts on: `main` as pushed to GitHub.
   So push `main` first. To label with an eval change that has not landed, push its
   branch and fill in the prompt's optional `Code:` line.
2. Start a cloud session on the repo and paste the prompt below.
3. The session runs `tools/nne_cloud_relabel.sh` once per chunk of at most 25 min,
   usually twice. Each chunk commits `nne-labels/parts` (14 MB in all) and
   `nne-labels/commit.txt` and pushes them to the session's branch, so nothing is
   lost when the VM is reclaimed.
4. Here: `make nne-retrain FROM=<the session's branch>`. It fetches the parts, merges
   them (game, ply, FEN and features come from the local FEN list), and relabels
   every 509th position locally (~1000) as a spot check. Every column must be equal,
   or it stops. Then it trains and checks as above. If the session stopped early, the
   same command labels the rest here first.
5. Delete the labels branch: `git push origin --delete <branch>`.

If a VM is reclaimed mid-run, start a new session with the same prompt and add the
`Labels so far:` line. The script carries on from the pushed parts.

```
Label the NNE dataset for Ascaniusfish (docs/NNE_RELABEL.md, "In a Claude cloud session").
1. Stay on your working branch, as it is. Only if a line below says so, first run
   `git fetch origin <that branch>` and `git reset --hard FETCH_HEAD`:
   Code: main             (or: branch <branch>, to label with that branch's code)
   Labels so far: none    (or: branch <labels branch>; then reset to it instead)
2. Run `tools/nne_cloud_relabel.sh` in the background (one call takes up to 27 min) and
   wait for it. Exit status 3 means a chunk is done and pushed; run it again. Stop when
   it prints "relabel: done".
3. Reply with your branch's name and the script's lines starting with cpu:,
   "workers done" and "label search time".
Change no file yourself: the script commits and pushes nne-labels/ on its own.
```

**A trial** of the round trip, a few minutes and no retraining: the same prompt with
step 2 replaced by "Run `MINUTES=1 tools/nne_cloud_relabel.sh` once; exit status 3 is
expected." Here, `git fetch origin <its branch>` and
`git show FETCH_HEAD:nne-labels/commit.txt` show that the labels arrived. Then delete
the branch.
