# Sistema de Triagem Automática de Logs

Sistema automático para monitorar logs de erro, aggregar por padrão, e rastrear regressões no repositório público `nuvio-native-legacy`.

## Visão Geral

A triagem funciona como um **filtro de inteligência**:
1. **Coleta**: Lê logs do banco D1 (Cloudflare)
2. **Agregação**: Agrupa por padrão, versão e plataforma
3. **Rastreamento**: Mantém histórico em tabela `triagem` do D1
4. **Relatório**: Publica resumo em issue fixa no GitHub (sem IDs pessoais)

### Fluxo de Dados

```
Aplicação (cliente)
    ↓
[botão "Enviar Registro"] → D1 tabela `registro`
    ↓
Workflow: issues.opened ou schedule (a cada 6h)
    ↓
tools/triagem.py
    ├─ Lê registros novos (id > ultimo_log)
    ├─ Detecta padrões conhecidos
    ├─ Agrupa por (padrao, versao, plataforma)
    ├─ Atualiza tabela `triagem` (estado, regressões)
    └─ Posta comentário em issue fixa "Relatório de logs"
```

## Banco de Dados

### Tabela `registro`
```sql
CREATE TABLE registro (
  id         INTEGER PRIMARY KEY,
  pessoa     TEXT NOT NULL,              -- user ID (hash, nunca público)
  versao     TEXT NOT NULL,              -- "1.4.5", "1.5.0", etc
  plataforma TEXT NOT NULL,              -- "tizen", "lg", "android", etc
  quando     TEXT NOT NULL,              -- "auto", "anterior", "manual"
  texto      TEXT NOT NULL,              -- log lines
  criado     INTEGER NOT NULL            -- unix timestamp
);
```

**Quando**:
- `auto` - envio periódico da sessão atual
- `anterior` - log da sessão anterior (normal ao abrir app)
- `manual` - enviado por pessoa, geralmente para mostrar problema

**Crash vs. Normal**:
- Sessão normal termina com `fim`
- Se reabre e mostra `[avisos] a sessao anterior (...) nao se despediu` → crash real
- Sem `[avisos]` → apenas log de sessão anterior (normal)

### Tabela `triagem`
```sql
CREATE TABLE triagem (
  id          INTEGER PRIMARY KEY,
  criado      INTEGER NOT NULL,          -- quando detectou
  ultimo_log  INTEGER NOT NULL,          -- max registro.id lido
  padrao      TEXT NOT NULL,             -- tipo de erro
  versao      TEXT NOT NULL DEFAULT '',
  plataforma  TEXT NOT NULL DEFAULT '',
  ocorrencias INTEGER NOT NULL DEFAULT 0,
  pessoas     INTEGER NOT NULL DEFAULT 0,
  issue       INTEGER,                   -- issue fixada se houver
  estado      TEXT NOT NULL DEFAULT 'novo'
  -- estado: novo, conhecido, corrigido-em-X.Y.Z, regrediu
);
```

## Padrões Conhecidos

O script detecta automaticamente:

| Padrão | Detecção | Ação |
|--------|----------|------|
| **Crash WASM** | `Aborted` ou `RuntimeError` | Samsung só (WASM) |
| **Sessão morta** | `[avisos] a sessao anterior (...) nao se despediu` | Processo travado |
| **Erro de player** | `[video] avplay erro` com `errorText ≠ "No Error"` | avplay falhando |
| **Legenda ASS** | `[mkvass]` ou `[legenda] fallback` | Issue #92 |
| **Montagem rejeitada** | `montagem descartada` (ler motivo) | (addons) e (identidade) esperados |
| **Caminho corrompido** | `decode falhou` com caminho inválido | Arquivo não começa por `/` ou `http` |
| **Debrid** | `[debrid]` com `PLAN_RESTRICTED` | Limite de P2P TorBox |
| **Travada longa** | `longtask-max=` > 2000ms | Samsung principalmente |
| **Catálogo cortado** | `[desc]` com `de fora por cota` | Issue #126 |
| **Diagnostico V2** | `diagnostico=v2:` | Contar `aplicacao` (aplicada, mantido, restaurada) |

## Configuração

### Variáveis de Ambiente

```bash
# Obrigatórias (GitHub Secrets)
CLOUDFLARE_API_TOKEN      # D1 write access
CLOUDFLARE_ACCOUNT_ID     # Cloudflare account

# Opcional (GitHub Secrets)
GITHUB_TOKEN              # Se não usar app auth
```

### Setup no GitHub

1. **Criar secrets** em Settings → Secrets and variables → Actions:
   ```
   CLOUDFLARE_API_TOKEN  (scope: D1 only)
   CLOUDFLARE_ACCOUNT_ID
   ```

2. **Instalar Claude App** se não estiver:
   - https://github.com/apps/claude/installations/select_target
   - (para que Claude possa postar comentários)

3. **Criar issue fixa** "Relatório de logs" com label `triagem`:
   - Pinnar no repo (Settings → Pinned Issues)
   - Workflow postarão nela

4. **Migrations D1**:
   ```bash
   cd servidor/recomendacoes
   npx wrangler d1 execute nuvio-recomendacoes --remote < migracao-004-triagem.sql
   ```

## Rotina de Triage

### Rodada Completa

```python
# 1. Obter último log processado
ultimo_id = SELECT MAX(ultimo_log) FROM triagem

# 2. Ler registros novos
registros = SELECT * FROM registro WHERE id > ultimo_id AND pessoa != 'trakt:iqui27'

# 3. Agrupar por padrão
grupos = {
  (padrao, versao, plataforma): {
    ocorrencias: N,
    pessoas: set(),
    maior_id: max_id
  }
}

# 4. Para cada grupo, verificar regressão
if padrao foi marcado como "corrigido-in-X.Y.Z" e volta na versão >= X:
  estado = "regrediu"
else:
  estado = "novo" ou manter anterior

# 5. Gravar em triagem
INSERT/UPDATE triagem

# 6. Gerar comentário GitHub (sem IDs pessoais)
POST /repos/.../issues/NNN/comments
```

### Comparação com Triagem Anterior

Ao detectar um padrão:
1. Checar em `triagem` se já existe para (padrão, versao, plataforma)
2. Se sim:
   - Manter `estado` anterior a menos que seja regressão
   - Atualizar `ocorrencias`, `pessoas`, `ultimo_log`
3. Se não:
   - `estado = 'novo'`

### Detecção de Regressão

Se `padrao` foi marcado `corrigido-in-X.Y.Z`:
- Se volta numa versão **≥ X**, marcar `regrediu`
- Se volta numa versão < X, é apenas reincidência (não é regressão)

## GitHub Comments

**O que é publicado** (sempre agregado, nunca pessoal):
```
## Relatório de Logs

- **Crash WASM**: 3 ocorrências, 2 pessoas
  - Plataformas: tizen 4.0, tizen 6.0
  - Versões: 1.4.5, 1.5.0

- **Legenda ASS**: 5 ocorrências, 4 pessoas
  - Plataformas: lg, samsung
  - Versões: 1.4.4, 1.4.5

### ⚠️ Regressões Detectadas
- Crash WASM retornou em 1.5.0/tizen
```

**O que NUNCA é publicado**:
- IDs de pessoa (`pessoa` field)
- Trechos de log com URLs (addon ou debrid)
- Tokens ou credenciais
- Paths locais completos
- Números de conta

## Troubleshooting

### Erro 7403 do Wrangler
```
[Error] 7403
```
Transitório. Script retentar automaticamente (3s delay).

### Timeout na Leitura de Registros

D1 é lento com dados grandes. Se houver muitos registros:
- Workflow limita a 180s (3min)
- Se exceder, pode ler em batches menores

### Issue de Triagem Não Encontrada

Se workflow não encontra issue com label `triagem`:
- Criar manualmente com título contendo "Relatório"
- Ou deixar criar na próxima rodada (quando houver padrão)

### Sem Acesso ao D1

Verificar:
```bash
npx wrangler d1 list --json
# Deve listar "nuvio-recomendacoes"

npx wrangler whoami
# Deve estar logado
```

## Desenvolvimento

### Testar Localmente

```bash
# Com arquivo JSON dump (não requer D1 access)
python3 tools/triagem.py --arquivo registros.json

# Modo teste (não escreve no D1)
python3 tools/triagem.py --test
```

### Adicionar Padrão

Em `tools/triagem.py`, adicionar em `PADROES`:
```python
PADROES = {
    ...
    "novo_texto_procurado": "descricao do padrão",
}
```

Depois o script detectará automaticamente em novos registros.

### Debug

```bash
# Ver output do workflow
gh workflow run triagem.yml -r seu-branch
gh run list -w triagem.yml
gh run view NNN --log
```

## Referências

- Issue #92: Legenda ASS em Samsung
- Issue #126: Catálogo cortado por limite
- Issue #129: Ajuste zerado (corrigido em 1.4.4)

---

*Última atualização: 2026-09-27*
