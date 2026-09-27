# Log Triage Quick Start

## ⚡ Fast Setup (5 minutes)

### 1. Environment Variables
Set up Cloudflare credentials:
```bash
export CLOUDFLARE_API_TOKEN="your-d1-token"
export CLOUDFLARE_ACCOUNT_ID="your-account-id"
```

### 2. Run Setup Script
```bash
tools/setup-triagem.sh
```

This will:
- ✓ Check prerequisites
- ✓ Apply D1 migrations
- ✓ Create GitHub labels
- ✓ Create triagem issue
- ✓ Verify everything works

### 3. Configure GitHub Secrets
In your GitHub repo (Settings → Secrets):
- Add `CLOUDFLARE_API_TOKEN`
- Add `CLOUDFLARE_ACCOUNT_ID`

Done! The system will now:
- Run on issues.opened webhook
- Run every 6 hours automatically
- Post summaries to the pinned issue

---

## 🧪 Test Locally

### Preview Mode (no database writes)
```bash
python3 tools/triagem.py --test
```

### Test with Sample Data
```bash
# Generate sample registros.json first
python3 tools/triagem.py --arquivo registros.json
```

---

## 📊 Monitor the System

### View Workflow Runs
```bash
gh workflow list
gh run list -w triagem.yml
gh run view <run-id> --log
```

### Check D1 Database
```bash
cd servidor/recomendacoes
npx wrangler d1 execute nuvio-recomendacoes --remote --json \
  --command "SELECT COUNT(*) as total FROM triagem"
```

### View Latest Triage Results
```bash
npx wrangler d1 execute nuvio-recomendacoes --remote --json \
  --command "SELECT padrao, versao, plataforma, ocorrencias, pessoas, estado 
            FROM triagem 
            ORDER BY criado DESC 
            LIMIT 10"
```

---

## 🔧 Troubleshooting

### "CLOUDFLARE_API_TOKEN not defined"
```bash
export CLOUDFLARE_API_TOKEN="your-token"
export CLOUDFLARE_ACCOUNT_ID="your-account-id"
```

### "7403 error from wrangler"
This is transient. The script retries automatically (3s delay).

### Workflow not running
Check:
1. `.github/workflows/triagem.yml` exists
2. GitHub Secrets are set (don't contain typos)
3. Claude GitHub App is installed on repo

### Want to manually trigger a run?
```bash
gh workflow run triagem.yml
```

---

## 📝 Understanding the Patterns

The system detects and aggregates:

| Pattern | What it means |
|---------|--------------|
| **Crash WASM** | Samsung app crashed (unhandled exception) |
| **Sessão morta** | App process died without cleanup |
| **Erro de player** | avplay returned an error |
| **Legenda ASS** | ASS subtitle rendering failed |
| **Montagem rejeitada** | Invalid addon or identity |
| **Caminho corrompido** | File path is invalid |
| **Debrid** | TorBox/P2P limit exceeded |
| **Travada longa** | UI blocked > 2000ms |

---

## 🔑 Key Concepts

### Automatic Aggregation
Logs are grouped by:
- **Pattern** (type of error)
- **Version** (app version)
- **Platform** (tizen, lg, android, etc.)

Then counted:
- **Ocorrências** (how many times)
- **Pessoas** (unique users)

### State Tracking
Each pattern has a state:
- `novo` - first time seeing it
- `conhecido` - we've seen it before
- `corrigido-em-X.Y.Z` - fixed in release X.Y.Z
- `regrediu` - came back in a newer version

### GitHub Comments
The system posts **aggregated only** summaries:
```
- **Crash WASM**: 3 ocorrências, 2 pessoas
  - Plataformas: tizen, samsung
  - Versões: 1.4.5

- **Erro de player**: 5 ocorrências, 4 pessoas
```

**Never** includes:
- Personal user IDs
- Addon URLs or credentials
- Debrid service credentials
- Specific file paths

---

## 📚 Full Documentation

See `TRIAGEM.md` for:
- Complete pattern descriptions
- Database schema details
- Regression detection logic
- GitHub integration details
- Development guide

```bash
cat TRIAGEM.md
```

---

## 💡 Tips

### Add a New Pattern
Edit `tools/triagem.py` and add to `PADROES`:
```python
PADROES = {
    ...
    "new_error_text": "description of pattern",
}
```

The script will detect it automatically in future logs.

### Mark as Fixed
After releasing a fix, manually update triagem table:
```sql
UPDATE triagem 
SET estado = 'corrigido-em-1.5.0' 
WHERE padrao = 'Crash WASM'
```

Next time it appears, it will be flagged as `regrediu`.

### Manual Issue
Users can send logs manually via app button. These are marked `quando='manual'` 
and processed first by the triage system.

---

## 🆘 Need Help?

1. Check `TRIAGEM.md` for detailed docs
2. Run `tools/setup-triagem.sh -h` for setup help
3. Test with `python3 tools/triagem.py --test`
4. View workflow logs: `gh run view <id> --log`

---

*Last updated: 2026-09-27*
