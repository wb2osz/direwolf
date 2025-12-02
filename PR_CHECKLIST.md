# Pre-PR Submission Checklist

## Before Creating the Pull Request

### 1. Code Quality
- [X] Run a final build test: `cd build && cmake .. && make`
- [X] Check for compiler warnings: `make 2>&1 | grep -i warning`
- [X] Verify no debug code left in: `grep -r "printf.*DEBUG\|fprintf.*stderr.*TEST" src/`

### 2. Testing
- [X] Test backward compatibility: `direwolf -t 0 < /dev/null` (should work as before)
- [X] Verify IQ mode: `cat test/iq/iq48k_cfloat.raw | ./build/src/direwolf -M -t 0 -r 48000 -n 1 iq:48000`
- [X] Verify IQ mode on the real world: `python3 scripts/sdrplay_to_direwolf.py --agc  2>/dev/null |  csdr fir_decimate_cc 4 2>/dev/null | ./build/src/direwolf -M -t 0 -r 48000 -n 1 iq:48000 2>&1 -`

### 3. Documentation
- [X] Review `IQ_INPUT.md` for accuracy
- [X] Check README.md mentions the new feature
- [X] Verify code comments are clear

### 4. Git Hygiene
- [X] Current branch is `iq_input_added`: `git branch --show-current`
- [X] All commits are clean: `git log --oneline origin/master..HEAD`
- [X] No unwanted files: `git status`
- [X] Commits have good messages: `git log --oneline -3`

### 5. GitHub PR Preparation

#### Option A: Via GitHub Web Interface
1. Go to: https://github.com/guido57/direwolf-iq
2. Click "Contribute" → "Open pull request"
3. Change base repository to: `wb2osz/direwolf`
4. Base branch: `master` (or `dev` if they have one)
5. Head repository: `guido57/direwolf-iq`
6. Compare branch: `iq_input_added`
7. Title: "Add IQ Input Support with RSSI/SNR Metrics"
8. Description: Copy from `PR_DESCRIPTION_SHORT.md` (or full `PULL_REQUEST.md`)

#### Option B: Via Command Line
```bash
# Install GitHub CLI if needed
sudo apt install gh

# Authenticate
gh auth login

# Create PR to upstream
gh pr create --repo wb2osz/direwolf \
  --base master \
  --head guido57:iq_input_added \
  --title "Add IQ Input Support with RSSI/SNR Metrics" \
  --body-file PR_DESCRIPTION_SHORT.md
```

### 6. Before Submitting - Check Upstream

- [ ] Check if wb2osz/direwolf has contribution guidelines:
  - Visit: https://github.com/wb2osz/direwolf/blob/master/CONTRIBUTING.md
  - Check issues for similar requests
  - Look at recent PRs to match style

- [ ] Check if there's a preferred development branch:
  - Look for `dev`, `development`, or `next` branch
  - Target that instead of `master` if it exists

- [ ] Review open issues:
  - Search for "IQ" or "SDR" related issues
  - Reference them in PR if found

### 7. PR Best Practices

#### In the PR Description, Mention:
- [ ] What problem this solves
- [ ] How it's been tested
- [ ] That it's backward compatible
- [ ] That you're open to feedback/changes

#### Be Prepared to:
- [ ] Answer questions about implementation
- [ ] Make requested changes
- [ ] Provide additional test results
- [ ] Split into smaller PRs if requested
- [ ] Rebase on latest master if needed

### 8. After Submitting

- [ ] Watch for CI/CD failures (if they have automated tests)
- [ ] Respond promptly to reviewer comments
- [ ] Be patient - maintainer review can take time
- [ ] Stay professional and thankful

## Quick Command Reference

```bash
# Verify you're on the right branch
git branch --show-current  # Should show: iq_input_added

# Check what will be in the PR
git diff --stat origin/master...HEAD

# View commits
git log --oneline origin/master..HEAD

# Test the build
cd build && cmake .. && make -j$(nproc)

# Run IQ test
cat ../test/iq/iq48k_cfloat.raw | ./src/direwolf -M -t 0 -r 48000 -n 1 iq:48000 2>&1 | head -20

# If you need to make changes before PR
git add <files>
git commit -m "Fix: description"
git push origin iq_input_added
```

## Notes

**Remember**: 
- The upstream maintainer (WB2OSZ) has the final say
- Be open to suggestions and changes
- Your fork (`guido57/direwolf-iq`) with `iq_rssi_added` branch still has all your experimental work
- This PR is just the core feature - web interface stays in your fork

**If PR is Accepted**:
- Congrats! Your code will be in the official Direwolf
- Update your fork: `git fetch upstream && git merge upstream/master`

**If PR Needs Changes**:
- Make changes on `iq_input_added` branch
- Push updates - they'll automatically appear in the PR
- `git push origin iq_input_added`

**If PR is Rejected**:
- Don't worry! You still have a great feature in your fork
- Users can use your fork directly
- You can maintain it independently

Good luck! 🚀
