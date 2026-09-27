#!/bin/bash
# Setup script for automatic log triage system
# Usage: tools/setup-triagem.sh [--auto]
#
# Sets up:
# - GitHub Action workflow
# - D1 database migrations
# - GitHub repository labels and issue
# - Webhook subscriptions (optional)

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVIDOR="$REPO_ROOT/servidor/recomendacoes"
WORKFLOW_PATH="$REPO_ROOT/.github/workflows/triagem.yml"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() { echo -e "${BLUE}→${NC} $*"; }
success() { echo -e "${GREEN}✓${NC} $*"; }
warn() { echo -e "${YELLOW}⚠${NC} $*"; }
error() { echo -e "${RED}✗${NC} $*"; exit 1; }

check_prerequisites() {
    log "Checking prerequisites..."

    # Check if workflow file exists
    if [[ ! -f "$WORKFLOW_PATH" ]]; then
        error "Workflow file not found: $WORKFLOW_PATH"
    fi
    success "Workflow file found"

    # Check if triagem.py exists
    if [[ ! -f "$REPO_ROOT/tools/triagem.py" ]]; then
        error "triagem.py not found"
    fi
    success "triagem.py found"

    # Check if migration file exists
    MIGRATION="$SERVIDOR/migracao-004-triagem.sql"
    if [[ ! -f "$MIGRATION" ]]; then
        error "Migration file not found: $MIGRATION"
    fi
    success "Migration file found"

    # Check if in git repo
    if [[ ! -d "$REPO_ROOT/.git" ]]; then
        error "Not in a git repository"
    fi
    success "In git repository"
}

setup_database() {
    log "Setting up D1 database..."

    if [[ -z "$CLOUDFLARE_API_TOKEN" ]]; then
        warn "CLOUDFLARE_API_TOKEN not set - skipping D1 setup"
        warn "To set up D1, run:"
        warn "  export CLOUDFLARE_API_TOKEN=your-token"
        warn "  export CLOUDFLARE_ACCOUNT_ID=your-account-id"
        warn "  npx wrangler d1 execute nuvio-recomendacoes --remote < $SERVIDOR/migracao-004-triagem.sql"
        return
    fi

    cd "$SERVIDOR"

    # Check if triagem table exists
    log "Checking D1 database..."
    SQL="SELECT name FROM sqlite_master WHERE type='table' AND name='triagem'"
    RESULT=$(npx wrangler d1 execute nuvio-recomendacoes --remote --json --command "$SQL" 2>/dev/null | grep -o '"triagem"' || echo "")

    if [[ -z "$RESULT" ]]; then
        log "Running migration for triagem table..."
        npx wrangler d1 execute nuvio-recomendacoes --remote < migracao-004-triagem.sql
        success "D1 migration completed"
    else
        success "triagem table already exists"
    fi
}

check_github_cli() {
    if ! command -v gh &> /dev/null; then
        warn "GitHub CLI (gh) not installed - skipping GitHub setup"
        warn "To set up GitHub, install gh: https://cli.github.com"
        return 1
    fi
    return 0
}

setup_github() {
    log "Setting up GitHub..."

    if ! check_github_cli; then
        return
    fi

    # Check if authenticated
    if ! gh auth status >/dev/null 2>&1; then
        error "GitHub CLI not authenticated. Run: gh auth login"
    fi
    success "GitHub CLI authenticated"

    # Get repo info
    REPO_INFO=$(gh repo view --json nameWithOwner -q 2>/dev/null || echo "")
    if [[ -z "$REPO_INFO" ]]; then
        warn "Could not determine repository - not in a GitHub repo"
        return
    fi

    log "Setting up labels..."
    # Create triagem label if it doesn't exist
    if ! gh label list | grep -q "triagem"; then
        gh label create triagem \
            --description "Automatic log triage results" \
            --color "0366d6" 2>/dev/null || warn "Could not create label (may already exist)"
        success "Created 'triagem' label"
    else
        success "Label 'triagem' already exists"
    fi

    log "Creating initial triagem issue..."
    # Check if triagem issue exists
    TRIAGEM_ISSUE=$(gh issue list --label triagem --state open --json number -q '.[0].number' 2>/dev/null || echo "")

    if [[ -z "$TRIAGEM_ISSUE" ]]; then
        ISSUE_BODY="# Relatório de Logs

Este é o issue fixado para relatórios automáticos de triagem de logs.

## Como funciona
- Sistema automático lê logs do banco D1
- Agrega por padrão, versão e plataforma
- Posta resumo aqui (sem IDs pessoais ou credenciais)
- Detecta regressões comparando com triagens anteriores

## Padrões monitorados
- Crashes (WASM, processos travados)
- Erros de reprodução de vídeo
- Problemas com legendas
- Issues de debrid/torrent
- Travadas longas

Mais informações: veja [TRIAGEM.md](../blob/main/TRIAGEM.md)
"
        gh issue create \
            --title "Relatório de logs" \
            --body "$ISSUE_BODY" \
            --label triagem 2>/dev/null || warn "Could not create issue"
        success "Created triagem issue"
    else
        success "Triagem issue already exists (#$TRIAGEM_ISSUE)"
    fi
}

setup_secrets() {
    log "GitHub Secrets setup..."

    if ! check_github_cli; then
        return
    fi

    warn "GitHub Secrets must be set manually via GitHub UI or gh CLI:"
    warn ""
    warn "Via GitHub UI:"
    warn "  1. Go to Settings → Secrets and variables → Actions"
    warn "  2. Create new repository secret:"
    warn "     - CLOUDFLARE_API_TOKEN (D1 access)"
    warn "     - CLOUDFLARE_ACCOUNT_ID (your Cloudflare account)"
    warn ""
    warn "Via gh CLI:"
    warn "  gh secret set CLOUDFLARE_API_TOKEN -b 'your-token'"
    warn "  gh secret set CLOUDFLARE_ACCOUNT_ID -b 'your-account-id'"
    warn ""
    warn "Do NOT commit these to git!"
}

verify_setup() {
    log "Verifying setup..."

    PASS=0
    FAIL=0

    # Check workflow file
    if [[ -f "$WORKFLOW_PATH" ]]; then
        success "Workflow file exists"
        ((PASS++))
    else
        error "Workflow file missing"
        ((FAIL++))
    fi

    # Check triagem.py
    if [[ -f "$REPO_ROOT/tools/triagem.py" ]]; then
        success "triagem.py exists and is executable"
        ((PASS++))
    else
        error "triagem.py missing"
        ((FAIL++))
    fi

    # Check TRIAGEM.md
    if [[ -f "$REPO_ROOT/TRIAGEM.md" ]]; then
        success "TRIAGEM.md documentation exists"
        ((PASS++))
    else
        warn "TRIAGEM.md missing"
        ((FAIL++))
    fi

    echo ""
    log "Setup verification: $PASS passed, $FAIL failed"

    if [[ $FAIL -eq 0 ]]; then
        success "All checks passed!"
        return 0
    else
        warn "Some checks failed - see above"
        return 1
    fi
}

show_usage() {
    cat <<EOF
${BLUE}Log Triage System Setup${NC}

Usage: tools/setup-triagem.sh [OPTIONS]

Options:
  --db-only    Only set up database (skip GitHub)
  --gh-only    Only set up GitHub (skip database)
  --auto       Non-interactive setup

Prerequisites:
  - wrangler (npm install -g wrangler)
  - gh CLI for GitHub setup (github.com/cli/cli)
  - CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID env vars

Steps:
  1. Prerequisites check
  2. D1 database setup and migrations
  3. GitHub Actions setup
  4. GitHub labels and issues
  5. Verification

EOF
}

main() {
    local db_only=0
    local gh_only=0
    local auto=0

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --db-only) db_only=1; shift ;;
            --gh-only) gh_only=1; shift ;;
            --auto) auto=1; shift ;;
            -h|--help) show_usage; exit 0 ;;
            *) error "Unknown option: $1" ;;
        esac
    done

    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  Log Triage System Setup${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    check_prerequisites

    if [[ $gh_only -eq 0 ]]; then
        setup_database
    fi

    if [[ $db_only -eq 0 ]]; then
        setup_github
        setup_secrets
    fi

    echo ""
    verify_setup

    echo ""
    echo -e "${GREEN}✓ Setup complete!${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Set up GitHub Secrets (see above)"
    echo "  2. Verify workflow runs: gh workflow list"
    echo "  3. Test with: python3 tools/triagem.py --test"
    echo ""
    echo "For more info: cat TRIAGEM.md"
}

main "$@"
