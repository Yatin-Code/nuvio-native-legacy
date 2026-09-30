#!/usr/bin/env node
/**
 * Triagem automática de logs - versão com melhor tratamento de buffer
 */

import { execSync, spawnSync } from 'child_process';
import { writeFileSync, readFileSync, unlinkSync } from 'fs';
import { randomBytes } from 'crypto';

const DB = 'nuvio-recomendacoes';
const TEMP_DIR = '/tmp';

function getTempFile() {
  return `${TEMP_DIR}/d1-${randomBytes(4).toString('hex')}.json`;
}

function executeD1QueryToFile(sql) {
  const tempFile = getTempFile();
  const escapeCmd = sql.replace(/"/g, '\\"').replace(/\$/g, '\\$');

  try {
    const cmd = `npx --yes wrangler d1 execute ${DB} --remote --json --command "${escapeCmd}" > ${tempFile} 2>&1`;
    const result = execSync(cmd, { stdio: 'pipe', shell: '/bin/bash' });

    const content = readFileSync(tempFile, 'utf8');
    const jsonMatch = content.match(/\[\n[\s\S]*?\n\]/);

    if (!jsonMatch) {
      throw new Error(`Invalid D1 response: ${content.substring(0, 200)}`);
    }

    const parsed = JSON.parse(jsonMatch[0]);
    unlinkSync(tempFile);
    return parsed[0]?.results || [];
  } catch (error) {
    try { unlinkSync(tempFile); } catch {}

    if (error.message.includes('7403')) {
      console.log('  ⏳ D1 transitório (7403), aguardando...');
      execSync('sleep 2', { stdio: 'pipe' });
      return executeD1QueryToFile(sql);
    }

    throw error;
  }
}

const KNOWN_PATTERNS = {
  'crash-wasm': /Aborted.*RuntimeError: crash WASM/i,
  'sessao-morta': /nao se despediu/i,
  'video-avplay-erro': /\[video\].*avplay.*erro/i,
  'legenda-ass': /\[(mkvass|legenda)\].*(?:fallback|falha)/i,
  'montagem-descartada': /montagem descartada/i,
  'decode-caminho': /decode falhou|caminho corrompido/i,
  'debrid-restricted': /\[debrid\].*PLAN_RESTRICTED|fora de cache/i,
  'longtask-travada': /longtask-max=\d+/i,
  'catalogo-cortado': /\[desc\].*fora por cota/i,
};

function detectPattern(text) {
  for (const [key, regex] of Object.entries(KNOWN_PATTERNS)) {
    if (regex.test(text)) return key;
  }
  return null;
}

function getLastTriageId() {
  const results = executeD1QueryToFile('SELECT MAX(ultimo_log) as max_id FROM triagem');
  return results[0]?.max_id || 0;
}

function getNewRecords(afterId) {
  const sql = `
    SELECT id, pessoa, versao, plataforma, quando, texto
    FROM registro
    WHERE id > ${afterId} AND pessoa != 'trakt:iqui27'
    ORDER BY id ASC
    LIMIT 100
  `;
  return executeD1QueryToFile(sql);
}

function triageRecords(records) {
  const map = new Map();

  records.forEach(record => {
    const pattern = detectPattern(record.texto);
    if (!pattern) return;

    const key = `${pattern}|${record.versao || '?'}|${record.plataforma || '?'}`;

    if (!map.has(key)) {
      map.set(key, {
        pattern,
        versao: record.versao || '',
        plataforma: record.plataforma || '',
        ocorrencias: 0,
        pessoas: new Set(),
        ultimoId: 0,
      });
    }

    const item = map.get(key);
    item.ocorrencias++;
    item.pessoas.add(record.pessoa);
    item.ultimoId = Math.max(item.ultimoId, record.id);
  });

  return Array.from(map.values());
}

function saveTriageResults(results) {
  const now = Math.floor(Date.now() / 1000);

  results.forEach(result => {
    const pattern = (result.pattern || '').replace(/'/g, "''");
    const versao = (result.versao || '').replace(/'/g, "''");
    const plataforma = (result.plataforma || '').replace(/'/g, "''");

    const sql = `INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma, ocorrencias, pessoas, estado) VALUES (${now}, ${result.ultimoId}, '${pattern}', '${versao}', '${plataforma}', ${result.ocorrencias}, ${result.pessoas.size}, 'novo')`;

    try {
      executeD1QueryToFile(sql);
    } catch (error) {
      console.error(`  ❌ Erro ao gravar ${pattern}:`, error.message);
    }
  });
}

function formatComment(results) {
  if (results.length === 0) {
    return '## 📊 Relatório de Logs\n\nNenhum novo registro.';
  }

  let text = '## 📊 Relatório de Logs\n\n';
  text += `| Padrão | Versão | Plataforma | Ocorrências | Pessoas |\n`;
  text += `|--------|--------|------------|-------------|----------|\n`;

  results.sort((a, b) => b.ocorrencias - a.ocorrencias).forEach(r => {
    text += `| ${r.pattern} | ${r.versao || '-'} | ${r.plataforma || '-'} | ${r.ocorrencias} | ${r.pessoas.size} |\n`;
  });

  return text;
}

async function main() {
  console.log('🔍 Triagem automática\n');

  try {
    const lastId = getLastTriageId();
    console.log(`  Último: ${lastId}`);

    const records = getNewRecords(lastId);
    console.log(`  Novos: ${records.length}`);

    if (records.length === 0) {
      console.log('  ✓ Nada novo');
      return;
    }

    const triageResults = triageRecords(records);
    console.log(`  Padrões: ${triageResults.length}`);

    saveTriageResults(triageResults);
    console.log(`  ✓ Gravado`);

    const comment = formatComment(triageResults);
    writeFileSync('/tmp/triagem-comment.md', comment);
    console.log(`  ✓ Comentário: /tmp/triagem-comment.md`);

    console.log('\n✓ OK');
  } catch (error) {
    console.error('❌', error.message);
    process.exit(1);
  }
}

main();
