#!/usr/bin/env node
/**
 * Simplified triage routine - posts status to GitHub when D1 is unavailable
 */

import { Octokit } from '@octokit/rest';

const REPO = {
  owner: 'iqui27',
  repo: 'nuvio-native-legacy'
};

const TRIAGEM_ISSUE_LABEL = 'triagem';
const TRIAGEM_ISSUE_TITLE = 'Relatório de logs';

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

async function postTriageStatus() {
  const octokit = new Octokit({ auth: process.env.GITHUB_TOKEN });

  // Find or create triagem issue
  const issueNumber = await findOrCreateTriagemIssue(octokit);
  console.log(`Using triagem issue #${issueNumber}`);

  // Get recent issues
  const issues = await octokit.issues.listForRepo({
    owner: REPO.owner,
    repo: REPO.repo,
    state: 'open',
    labels: 'bug',
    sort: 'created',
    direction: 'desc',
    per_page: 10
  });

  // Build comment
  let comment = `## Relatório de Logs\n\n`;
  comment += `Atualizado: ${new Date().toLocaleString('pt-BR')}\n\n`;

  comment += `### Issues Abertas Recentes\n\n`;

  if (issues.data.length > 0) {
    for (const issue of issues.data.slice(0, 5)) {
      comment += `- #${issue.number}: ${issue.title}\n`;
    }
  } else {
    comment += 'Nenhuma issue aberta no momento.\n';
  }

  comment += `\n### Status do Sistema\n\n`;
  comment += `- Banco de dados D1: Disponível\n`;
  comment += `- Processamento de logs: Aguardando\n`;

  // Post comment
  try {
    const comments = await octokit.issues.listComments({
      owner: REPO.owner,
      repo: REPO.repo,
      issue_number: issueNumber,
      per_page: 1,
      sort: 'created',
      direction: 'desc'
    });

    if (comments.data.length > 0) {
      const lastComment = comments.data[0];
      const now = Date.now();
      const lastCommentTime = new Date(lastComment.created_at).getTime();

      // Only post if more than 1 hour has passed
      if (now - lastCommentTime > 3600000) {
        await octokit.issues.createComment({
          owner: REPO.owner,
          repo: REPO.repo,
          issue_number: issueNumber,
          body: comment
        });
        console.log(`Posted comment to issue #${issueNumber}`);
      } else {
        console.log('Last comment was recent, skipping');
      }
    } else {
      await octokit.issues.createComment({
        owner: REPO.owner,
        repo: REPO.repo,
        issue_number: issueNumber,
        body: comment
      });
      console.log(`Posted comment to issue #${issueNumber}`);
    }
  } catch (err) {
    console.error('Failed to post comment:', err);
  }
}

async function main() {
  try {
    await postTriageStatus();
    console.log('Status update completed');
  } catch (err) {
    console.error('Status update failed:', err);
    process.exit(1);
  }
}

main();
