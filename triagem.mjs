#!/usr/bin/env node
/**
 * Automatic log triage routine for Nuvio
 * Reads new logs from D1, groups by pattern, updates triagem table,
 * and posts aggregated results to GitHub
 */

import { execSync } from 'child_process';
import { Octokit } from '@octokit/rest';

const REPO = {
  owner: 'iqui27',
  repo: 'nuvio-native-legacy'
};

const TRIAGEM_ISSUE_LABEL = 'triagem';
const TRIAGEM_ISSUE_TITLE = 'Relatório de logs';

// Known patterns to identify in logs
const KNOWN_PATTERNS = {
  'crash-wasm': /Aborted.*RuntimeError/,
  'sessao-morreu': /nao se despediu/,
  'erro-video': /\[video\].*avplay erro.*errorText.*(?!No Error)/,
  'legenda-ass': /\[(?:mkvass|legenda)\].*(?:falha|fallback para a TV)/,
  'montagem-descartada': /montagem descartada/,
  'decode-falhou': /decode falhou com caminho(?!^\/|http)/,
  'debrid-torbox': /\[debrid\].*(?:TorBox|PLAN_RESTRICTED)/,
  'longtask': /longtask-max=\s*([0-9]+)/,
  'cota-addon': /\[desc\].*de fora por cota/,
  'ajuste-zerado': /ajuste zerado/
};

async function executeD1(command) {
  try {
    const output = execSync(
      `npx --yes wrangler d1 execute nuvio-recomendacoes --remote --json --command "${command.replace(/"/g, '\\"')}"`,
      {
        cwd: '/home/user/nuvio-native-legacy/servidor/recomendacoes',
        encoding: 'utf-8',
        timeout: 30000
      }
    );

    // Skip noise prefix and find JSON
    const lines = output.trim().split('\n');
    let jsonStart = -1;
    for (let i = 0; i < lines.length; i++) {
      if (lines[i].startsWith('[') || lines[i].startsWith('{')) {
        jsonStart = i;
        break;
      }
    }

    if (jsonStart === -1) {
      console.error('D1 output:', output);
      throw new Error('No JSON found in D1 output');
    }

    return JSON.parse(lines.slice(jsonStart).join('\n'));
  } catch (err) {
    if (err.message.includes('7403')) {
      console.log('D1 error 7403 (transient), retrying...');
      await new Promise(r => setTimeout(r, 2000));
      return executeD1(command);
    }
    throw err;
  }
}

function identifyPattern(texto) {
  for (const [patternName, regex] of Object.entries(KNOWN_PATTERNS)) {
    if (regex.test(texto)) {
      return patternName;
    }
  }

  // Extract generic pattern from first line or diagnostico
  if (texto.startsWith('diagnostico=v2:')) {
    return 'diagnostico-v2';
  }

  const firstLine = texto.split('\n')[0];
  if (firstLine.length > 0) {
    return firstLine.substring(0, 50);
  }

  return 'desconhecido';
}

function extractVersion(versao) {
  return versao || '';
}

function extractPlatform(plataforma) {
  return plataforma || '';
}

async function triageNewLogs() {
  console.log('Starting log triage routine...');

  // Get the last processed log ID
  const lastResult = await executeD1('SELECT COALESCE(MAX(ultimo_log), 0) as last_id FROM triagem');
  const lastId = lastResult[0]?.last_id || 0;
  console.log(`Last processed log ID: ${lastId}`);

  // Get new logs
  const logsResult = await executeD1(
    `SELECT id, pessoa, versao, plataforma, quando, texto FROM registro WHERE id > ${lastId} AND pessoa != 'trakt:iqui27' ORDER BY id ASC`
  );

  if (!logsResult || logsResult.length === 0) {
    console.log('No new logs to process');
    return { patterns: {}, maxId: lastId };
  }

  console.log(`Found ${logsResult.length} new logs to process`);

  // Group by pattern, version, platform
  const grouped = {};
  const peopleSet = new Set();

  for (const log of logsResult) {
    const pattern = identifyPattern(log.texto);
    const version = extractVersion(log.versao);
    const platform = extractPlatform(log.plataforma);
    const key = `${pattern}|${version}|${platform}`;

    if (!grouped[key]) {
      grouped[key] = {
        pattern,
        version,
        platform,
        count: 0,
        people: new Set(),
        lastId: log.id
      };
    }

    grouped[key].count++;
    grouped[key].people.add(log.pessoa);
    grouped[key].lastId = Math.max(grouped[key].lastId, log.id);
  }

  const maxId = Math.max(...logsResult.map(l => l.id));

  // Update or insert into triagem table
  for (const [key, data] of Object.entries(grouped)) {
    const now = Math.floor(Date.now() / 1000);

    // Check if pattern exists
    const existingResult = await executeD1(
      `SELECT id, estado FROM triagem WHERE padrao = '${data.pattern.replace(/'/g, "''")}' AND versao = '${data.version.replace(/'/g, "''")}' AND plataforma = '${data.platform.replace(/'/g, "''")}'`
    );

    if (existingResult && existingResult.length > 0) {
      // Update existing
      const existingId = existingResult[0].id;
      const previousEstado = existingResult[0].estado;

      let newEstado = previousEstado;
      if (previousEstado.startsWith('corrigido-em-')) {
        // Check if this version is >= the fix version
        const fixVersion = previousEstado.replace('corrigido-em-', '');
        if (data.version >= fixVersion) {
          newEstado = 'regrediu';
        }
      }

      await executeD1(
        `UPDATE triagem SET ultimo_log = ${data.lastId}, ocorrencias = ${data.count}, pessoas = ${data.people.size}, estado = '${newEstado}' WHERE id = ${existingId}`
      );
    } else {
      // Insert new
      await executeD1(
        `INSERT INTO triagem (criado, ultimo_log, padrao, versao, plataforma, ocorrencias, pessoas, estado) VALUES (${now}, ${data.lastId}, '${data.pattern.replace(/'/g, "''")}', '${data.version.replace(/'/g, "''")}', '${data.platform.replace(/'/g, "''")}', ${data.count}, ${data.people.size}, 'novo')`
      );
    }
  }

  return {
    patterns: grouped,
    maxId,
    logsProcessed: logsResult.length
  };
}

async function getOpenIssues() {
  const octokit = new Octokit({ auth: process.env.GITHUB_TOKEN });

  const issues = await octokit.issues.listForRepo({
    owner: REPO.owner,
    repo: REPO.repo,
    state: 'open',
    per_page: 100
  });

  return issues.data;
}

async function findOrCreateTriagemIssue(octokit) {
  const issues = await octokit.issues.listForRepo({
    owner: REPO.owner,
    repo: REPO.repo,
    labels: TRIAGEM_ISSUE_LABEL,
    state: 'open'
  });

  if (issues.data.length > 0) {
    return issues.data[0].number;
  }

  // Create the issue
  const newIssue = await octokit.issues.create({
    owner: REPO.owner,
    repo: REPO.repo,
    title: TRIAGEM_ISSUE_TITLE,
    body: 'Agregado de logs do Nuvio - atualizado automaticamente',
    labels: [TRIAGEM_ISSUE_LABEL]
  });

  return newIssue.data.number;
}

async function postTriageComment(patterns, logsProcessed) {
  if (logsProcessed === 0) {
    console.log('No new patterns to report');
    return;
  }

  const octokit = new Octokit({ auth: process.env.GITHUB_TOKEN });

  // Find or create triagem issue
  const issueNumber = await findOrCreateTriagemIssue(octokit);
  console.log(`Using triagem issue #${issueNumber}`);

  // Get all open issues to link patterns
  const openIssues = await getOpenIssues();
  const issuesByNumber = {};
  openIssues.forEach(i => issuesByNumber[i.number] = i);

  // Build comment
  let comment = `## Relatório de Logs\n\n`;
  comment += `Última atualização: ${new Date().toLocaleString()}\n\n`;
  comment += `**Novos logs processados:** ${logsProcessed}\n\n`;

  comment += `### Padrões Identificados\n\n`;

  const patternsSummary = {};
  for (const data of Object.values(patterns)) {
    const key = `${data.pattern}|${data.version}|${data.platform}`;
    if (!patternsSummary[data.pattern]) {
      patternsSummary[data.pattern] = {
        total: 0,
        versions: {},
        platforms: new Set()
      };
    }
    patternsSummary[data.pattern].total += data.count;
    patternsSummary[data.pattern].versions[data.version] = (patternsSummary[data.pattern].versions[data.version] || 0) + data.count;
    patternsSummary[data.pattern].platforms.add(data.platform);
  }

  for (const [pattern, summary] of Object.entries(patternsSummary)) {
    comment += `- **${pattern}**: ${summary.total} ocorrências\n`;
    for (const [version, count] of Object.entries(summary.versions)) {
      if (version) comment += `  - v${version}: ${count}\n`;
    }
    comment += `  - Plataformas: ${Array.from(summary.platforms).filter(p => p).join(', ') || 'desconhecida'}\n`;
  }

  // Post comment
  await octokit.issues.createComment({
    owner: REPO.owner,
    repo: REPO.repo,
    issue_number: issueNumber,
    body: comment
  });

  console.log(`Posted comment to issue #${issueNumber}`);
}

async function main() {
  try {
    const result = await triageNewLogs();

    if (result.logsProcessed > 0) {
      await postTriageComment(result.patterns, result.logsProcessed);
      console.log('Triage routine completed successfully');
    } else {
      console.log('No new logs to process');
    }
  } catch (err) {
    console.error('Triage routine failed:', err);
    process.exit(1);
  }
}

main();
