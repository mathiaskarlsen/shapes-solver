(() => {
  'use strict';

  const ROWS = 9;
  const COLUMNS = 7;
  const COLORS = { P: 'pink', B: 'blue', G: 'green', O: 'orange' };
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const boardElement = document.querySelector('#board');
  const overlay = document.querySelector('#tile-overlay');
  const palette = [...document.querySelectorAll('[data-color]')];
  const solveButton = document.querySelector('#solve-button');
  const editButton = document.querySelector('#edit-button');
  const resetButton = document.querySelector('#reset-button');
  const clearButton = document.querySelector('#clear-button');
  const progress = document.querySelector('#progress');
  const filledCount = document.querySelector('#filled-count');
  const errorMessage = document.querySelector('#error-message');

  let board = Array(ROWS * COLUMNS).fill('.');
  let savedBoard = null;
  let brush = 'P';
  let mode = 'editing';
  let solution = null;
  let stepIndex = 0;
  let positions = new Map();
  const tiles = new Map();
  let request = null;
  let epoch = 0;

  const pause = (ms) => new Promise((resolve) => setTimeout(resolve, reducedMotion.matches ? 0 : ms));

  function cancelActivity() {
    epoch++;
    request?.abort();
    request = null;
    overlay.replaceChildren();
    tiles.clear();
    positions = new Map();
    solution = null;
    stepIndex = 0;
    mode = 'editing';
  }

  function dismissError() {
    errorMessage.hidden = true;
    errorMessage.textContent = '';
  }

  function render() {
    const active = mode === 'guided' ? solution.steps[stepIndex] : null;
    const group = active ? new Set(active.removed) : null;
    const occupied = new Map();
    if (mode !== 'editing') {
      for (const [id, row] of positions) occupied.set(row * COLUMNS + id % COLUMNS, id);
    }

    for (const [id, tile] of tiles) {
      tile.classList.toggle('is-target', Boolean(group?.has(id)));
    }

    for (let index = 0; index < board.length; index++) {
      const cell = boardElement.children[index];
      const row = Math.floor(index / COLUMNS);
      const column = index % COLUMNS;
      const id = occupied.get(index);
      const color = mode === 'editing' ? board[index] : id === undefined ? '.' : board[id];
      const target = active && id !== undefined && group.has(id);
      cell.className = 'cell' + (mode === 'editing' && color !== '.' ? ` paint-${color} filled` : '') + (target ? ' is-target' : '');
      cell.textContent = mode === 'editing' && color !== '.' ? color : '';
      cell.disabled = mode === 'requesting' || mode === 'settling' || mode === 'animating';
      cell.setAttribute('aria-label', `Row ${row + 1}, column ${column + 1}: ${color === '.' ? 'empty' : COLORS[color] + ' tile'}${target ? ', recommended group' : ''}`);
    }

    const filled = board.filter((color) => color !== '.').length;
    const visible = mode === 'editing' || mode === 'requesting' ? filled : positions.size;
    filledCount.textContent = `${visible} / 63`;
    solveButton.disabled = filled === 0 || mode !== 'editing';
    editButton.hidden = mode === 'editing' || mode === 'requesting';
    palette.forEach((button) => button.setAttribute('aria-pressed', String(button.dataset.color === brush)));
    if (mode === 'requesting' || mode === 'settling') progress.textContent = '…';
    else if (mode === 'guided' || mode === 'animating') progress.textContent = `${stepIndex + 1} / ${solution.length}`;
    else if (mode === 'done') progress.textContent = `${solution.length} / ${solution.length}`;
    else progress.textContent = '—';
  }

  function editOriginal() {
    const original = mode === 'requesting' ? null : savedBoard;
    cancelActivity();
    if (original) board = [...original];
    dismissError();
    render();
  }

  function paint(index) {
    if (mode === 'requesting' || mode === 'settling' || mode === 'animating') return;
    if (mode !== 'editing') editOriginal();
    board[index] = brush;
    dismissError();
    render();
  }

  function selectBrush(color) {
    if (mode !== 'editing') editOriginal();
    brush = color;
    render();
  }

  function parsePositions(entries, expected) {
    if (!Array.isArray(entries) || entries.length !== expected.size) throw new Error('The solver returned an incomplete board.');
    const result = new Map();
    const slots = new Set();
    for (const entry of entries) {
      const { id, row } = entry ?? {};
      if (!Number.isInteger(id) || !expected.has(id) || result.has(id) || !Number.isInteger(row) || row < 0 || row >= ROWS) {
        throw new Error('The solver returned an invalid tile position.');
      }
      const slot = row * COLUMNS + id % COLUMNS;
      if (slots.has(slot)) throw new Error('The solver returned overlapping tiles.');
      slots.add(slot);
      result.set(id, row);
    }
    return result;
  }

  function parseSolution(data, source) {
    if (!data || !Number.isInteger(data.length) || data.length < 0 || !Array.isArray(data.steps) || data.steps.length !== data.length) {
      throw new Error('The solver returned an unexpected response.');
    }
    const originalIds = new Set(source.flatMap((color, id) => color === '.' ? [] : [id]));
    let previous = parsePositions(data.initial, originalIds);
    const steps = data.steps.map((step) => {
      if (!step || !Number.isInteger(step.row) || step.row < 0 || step.row >= ROWS || !Number.isInteger(step.column) || step.column < 0 || step.column >= COLUMNS || !COLORS[step.color] || !Array.isArray(step.removed) || !Number.isInteger(step.size) || step.size !== step.removed.length || step.size < 1) {
        throw new Error('The solver returned an invalid move.');
      }
      const removed = new Set(step.removed);
      if (removed.size !== step.size || [...removed].some((id) => !previous.has(id) || source[id] !== step.color) || ![...removed].some((id) => id % COLUMNS === step.column && previous.get(id) === step.row)) {
        throw new Error('The solver returned an invalid tile group.');
      }
      const remaining = new Set([...previous.keys()].filter((id) => !removed.has(id)));
      const next = parsePositions(step.positions, remaining);
      previous = next;
      return { ...step, removed: [...removed], positions: next };
    });
    if (previous.size !== 0) throw new Error('The solver returned an incomplete solution.');
    return { length: data.length, initial: parsePositions(data.initial, originalIds), steps };
  }

  function setTilePositions(next) {
    for (const [id, row] of next) {
      tiles.get(id).style.setProperty('--row', row);
    }
    positions = new Map(next);
  }

  function showTiles(initial) {
    const fragment = document.createDocumentFragment();
    for (const [id] of initial) {
      const tile = document.createElement('div');
      tile.className = `tile paint-${board[id]}`;
      tile.style.setProperty('--col', id % COLUMNS);
      tile.style.setProperty('--row', Math.floor(id / COLUMNS));
      tile.textContent = board[id];
      tiles.set(id, tile);
      fragment.append(tile);
    }
    overlay.replaceChildren(fragment);
    // Commit the original coordinates before changing the target rows so gravity is visible.
    void overlay.offsetWidth;
    setTilePositions(initial);
  }

  async function solve() {
    if (mode !== 'editing' || board.every((color) => color === '.')) return;
    dismissError();
    const source = [...board];
    const mine = ++epoch;
    const controller = new AbortController();
    request = controller;
    mode = 'requesting';
    render();
    try {
      const rows = Array.from({ length: ROWS }, (_, row) => source.slice(row * COLUMNS, (row + 1) * COLUMNS).join(''));
      const response = await fetch('/api/solve', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ board: rows }),
        signal: controller.signal
      });
      let data;
      try { data = await response.json(); }
      catch { throw new Error('The solver sent an unreadable response. Please try again.'); }
      if (!response.ok) throw new Error(typeof data.error === 'string' ? data.error : 'The solver could not solve this board.');
      if (mine !== epoch) return;
      solution = parseSolution(data, source);
      savedBoard = source;
      stepIndex = 0;
      request = null;
      mode = 'settling';
      showTiles(solution.initial);
      render();
      await pause(600);
      if (mine !== epoch) return;
      mode = solution.length ? 'guided' : 'done';
      render();
    } catch (error) {
      if (mine !== epoch || error.name === 'AbortError') return;
      request = null;
      mode = 'editing';
      overlay.replaceChildren();
      tiles.clear();
      positions.clear();
      solution = null;
      errorMessage.textContent = error instanceof TypeError ? 'Could not reach the solver. Check your connection and try again.' : error.message;
      errorMessage.hidden = false;
      render();
    }
  }

  async function followStep() {
    if (mode !== 'guided') return;
    const mine = epoch;
    const step = solution.steps[stepIndex];
    mode = 'animating';
    render();
    for (const id of step.removed) tiles.get(id).classList.add('removing');
    await pause(230);
    if (mine !== epoch) return;
    for (const id of step.removed) {
      tiles.get(id).remove();
      tiles.delete(id);
    }
    setTilePositions(step.positions);
    await pause(580);
    if (mine !== epoch) return;
    stepIndex++;
    mode = stepIndex === solution.length ? 'done' : 'guided';
    render();
  }

  for (let index = 0; index < ROWS * COLUMNS; index++) {
    const cell = document.createElement('button');
    cell.type = 'button';
    cell.className = 'cell';
    cell.addEventListener('click', () => {
      if (mode === 'guided') {
        const row = Math.floor(index / COLUMNS);
        const column = index % COLUMNS;
        const id = [...positions].find(([tileId, settledRow]) => settledRow === row && tileId % COLUMNS === column)?.[0];
        if (solution.steps[stepIndex].removed.includes(id)) void followStep();
        return;
      }
      if (mode === 'editing') paint(index);
    });
    boardElement.append(cell);
  }
  palette.forEach((button) => button.addEventListener('click', () => selectBrush(button.dataset.color)));
  solveButton.addEventListener('click', () => void solve());
  editButton.addEventListener('click', editOriginal);
  clearButton.addEventListener('click', () => {
    cancelActivity();
    board.fill('.');
    dismissError();
    render();
  });
  resetButton.addEventListener('click', () => {
    cancelActivity();
    board = savedBoard ? [...savedBoard] : Array(ROWS * COLUMNS).fill('.');
    dismissError();
    render();
  });
  render();
})();
