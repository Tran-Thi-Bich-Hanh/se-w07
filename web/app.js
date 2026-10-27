// TaskMaster V4 Kanban & P2P Frontend Controller
document.addEventListener('DOMContentLoaded', () => {
    let tasks = [];
    let p2pStatus = { nodeId: '', peers: [] };

    // DOM Elements
    const listPending = document.getElementById('list-pending');
    const listCompleted = document.getElementById('list-completed');
    const countPending = document.getElementById('count-pending');
    const countCompleted = document.getElementById('count-completed');
    
    const addTaskForm = document.getElementById('add-task-form');
    const taskTitleInput = document.getElementById('task-title');
    const taskDescInput = document.getElementById('task-desc');
    
    const btnSyncP2p = document.getElementById('btn-sync-p2p');
    const connectionStatus = document.getElementById('connection-status');
    const toast = document.getElementById('toast');

    // P2P DOM Elements
    const localNodeId = document.getElementById('local-node-id');
    const localHttpPort = document.getElementById('local-http-port');
    const localP2pPort = document.getElementById('local-p2p-port');
    const localUdpPort = document.getElementById('local-udp-port');
    const peerCount = document.getElementById('peer-count');
    const peersContainer = document.getElementById('peers-container');

    // Toast feedback helper
    function showToast(message, type = 'success') {
        toast.textContent = message;
        toast.className = `toast show ${type}`;
        setTimeout(() => {
            toast.className = 'toast';
        }, 3200);
    }

    // Format Timestamp
    function formatTime(timestamp) {
        if (!timestamp) return 'Vừa xong';
        // Check if ms or s
        const ts = timestamp > 10000000000 ? timestamp : timestamp * 1000;
        const date = new Date(ts);
        return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) + ' ' + date.toLocaleDateString();
    }

    // Update connection status UI
    function setServerOnline(online) {
        if (online) {
            connectionStatus.className = 'status-tag online';
            connectionStatus.innerHTML = '<span class="pulse-indicator"></span> Node Online';
        } else {
            connectionStatus.className = 'status-tag offline';
            connectionStatus.innerHTML = '<span class="pulse-indicator"></span> Mất kết nối';
        }
    }

    // Helper: Escape HTML to prevent XSS
    function escapeHtml(text) {
        if (!text) return '';
        const map = {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#039;'
        };
        return text.replace(/[&<>"']/g, m => map[m]);
    }

    // Fetch All Tasks from SQLite API
    async function loadTasks() {
        try {
            const response = await fetch('/api/tasks');
            if (!response.ok) throw new Error('Network error');
            tasks = await response.json();
            setServerOnline(true);
            renderKanban();
        } catch (error) {
            console.error('Lỗi tải tasks:', error);
            setServerOnline(false);
        }
    }

    // Fetch P2P Network Status (UDP/TCP discovery info)
    async function loadP2pStatus() {
        try {
            const response = await fetch('/api/p2p/status');
            if (!response.ok) return;
            p2pStatus = await response.json();

            localNodeId.textContent = p2pStatus.nodeId || 'Chưa xác định';
            localHttpPort.textContent = p2pStatus.httpPort || '-';
            localP2pPort.textContent = p2pStatus.p2pPort || '-';
            localUdpPort.textContent = p2pStatus.udpPort || '8888';
            peerCount.textContent = p2pStatus.peerCount || 0;

            renderPeers(p2pStatus.peers || []);
        } catch (err) {
            console.warn('Lỗi đọc trạng thái P2P:', err);
        }
    }

    // Render Peer Cards in P2P Monitor Panel
    function renderPeers(peers) {
        if (peers.length === 0) {
            peersContainer.innerHTML = `
                <div class="no-peers-msg">
                    <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/></svg>
                    Đang tìm kiếm peer khác trong mạng LAN qua UDP Broadcast (Port 8888)...
                </div>
            `;
            return;
        }

        peersContainer.innerHTML = peers.map(p => `
            <div class="peer-badge-card" title="Node ID: ${p.nodeId}">
                <span class="pulse-indicator" style="background:#10b981;box-shadow:0 0 6px #10b981;"></span>
                <span class="peer-badge-name">${escapeHtml(p.nodeName)}</span>
                <span class="peer-badge-endpoint">${p.ip}:${p.p2pPort}</span>
            </div>
        `).join('');
    }

    // Render Kanban 2-Column Board
    function renderKanban() {
        const pending = tasks.filter(t => !t.done);
        const completed = tasks.filter(t => t.done);

        countPending.textContent = pending.length;
        countCompleted.textContent = completed.length;

        // Render Pending Column
        if (pending.length === 0) {
            listPending.innerHTML = `
                <div class="empty-col">
                    <div class="empty-col-icon">✨</div>
                    <p>Không có công việc nào cần làm!</p>
                </div>
            `;
        } else {
            listPending.innerHTML = pending.map(t => renderCardHtml(t)).join('');
        }

        // Render Completed Column
        if (completed.length === 0) {
            listCompleted.innerHTML = `
                <div class="empty-col">
                    <div class="empty-col-icon">⏳</div>
                    <p>Chưa có công việc hoàn thành.</p>
                </div>
            `;
        } else {
            listCompleted.innerHTML = completed.map(t => renderCardHtml(t)).join('');
        }
    }

    function renderCardHtml(t) {
        const nodeLabel = t.originNodeId ? t.originNodeId.substring(0, 12) : 'local';
        return `
            <div class="kanban-card ${t.done ? 'completed' : ''}" data-id="${t.id}">
                <div class="card-top">
                    <h3 class="card-title-text">${escapeHtml(t.title)}</h3>
                </div>
                ${t.description ? `<p class="card-desc-text">${escapeHtml(t.description)}</p>` : ''}
                <div class="card-footer">
                    <div class="card-meta">
                        <span class="node-tag" title="Origin Node: ${escapeHtml(t.originNodeId)}">Node: ${escapeHtml(nodeLabel)}</span>
                        <span>${formatTime(t.updatedAt || t.createdAt)}</span>
                    </div>
                    <div class="card-actions">
                        <button class="btn-card-toggle" data-id="${t.id}" data-done="${t.done}">
                            ${t.done ? '↩ Mở lại' : '✓ Hoàn thành'}
                        </button>
                        <button class="btn-card-delete" data-id="${t.id}" title="Xóa công việc">
                            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                                <polyline points="3 6 5 6 21 6"></polyline>
                                <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path>
                            </svg>
                        </button>
                    </div>
                </div>
            </div>
        `;
    }

    // Add Task Handler
    addTaskForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const title = taskTitleInput.value.trim();
        const description = taskDescInput.value.trim();

        if (!title) return;

        try {
            const response = await fetch('/api/tasks', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ title, description })
            });

            if (!response.ok) throw new Error('Failed to create task');

            const newTask = await response.json();
            tasks.unshift(newTask);
            renderKanban();

            taskTitleInput.value = '';
            taskDescInput.value = '';
            taskTitleInput.focus();
            showToast('Đã thêm task và phát tán P2P tới các peers!', 'success');
        } catch (error) {
            console.error('Error adding task:', error);
            showToast('Lỗi khi thêm công việc!', 'error');
        }
    });

    // Delegation for Task actions on Kanban board
    document.querySelector('.kanban-section').addEventListener('click', async (e) => {
        const toggleBtn = e.target.closest('.btn-card-toggle');
        const deleteBtn = e.target.closest('.btn-card-delete');

        if (toggleBtn) {
            const id = toggleBtn.getAttribute('data-id');
            const currentDone = toggleBtn.getAttribute('data-done') === 'true';
            const target = tasks.find(t => t.id === id);
            if (!target) return;

            const newDone = !currentDone;

            try {
                const response = await fetch('/api/tasks', {
                    method: 'PUT',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id, done: newDone })
                });

                if (!response.ok) throw new Error('Failed to update task');

                target.done = newDone;
                target.updatedAt = Date.now();
                renderKanban();
                showToast(newDone ? 'Đã chuyển sang Đã Hoàn Thành!' : 'Đã chuyển về Cần Làm!', 'info');
            } catch (error) {
                console.error('Error toggling task:', error);
                showToast('Lỗi cập nhật task!', 'error');
            }
            return;
        }

        if (deleteBtn) {
            const id = deleteBtn.getAttribute('data-id');
            if (!confirm('Bạn có chắc muốn xóa công việc này?')) return;

            try {
                const response = await fetch(`/api/tasks?id=${encodeURIComponent(id)}`, {
                    method: 'DELETE'
                });

                if (!response.ok) throw new Error('Failed to delete task');

                tasks = tasks.filter(t => t.id !== id);
                renderKanban();
                showToast('Đã xóa công việc (tombstone đồng bộ P2P)!', 'info');
            } catch (error) {
                console.error('Error deleting task:', error);
                showToast('Lỗi xóa task!', 'error');
            }
        }
    });

    // Manual P2P Sync Button
    btnSyncP2p.addEventListener('click', async () => {
        btnSyncP2p.disabled = true;
        btnSyncP2p.innerHTML = '<span class="pulse-indicator"></span> Đang đồng bộ...';

        try {
            const res = await fetch('/api/p2p/sync', { method: 'POST' });
            const data = await res.json();
            showToast(`Đã hoàn tất đồng bộ với ${data.syncedPeers} peer(s)!`, 'success');
            await loadTasks();
            await loadP2pStatus();
        } catch (err) {
            showToast('Lỗi kết nối P2P!', 'error');
        } finally {
            btnSyncP2p.disabled = false;
            btnSyncP2p.innerHTML = `
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                    <path d="M21.5 2v6h-6M21.34 15.57a10 10 0 1 1-.57-8.38l5.67-5.67"/>
                </svg>
                Đồng Bộ P2P
            `;
        }
    });

    // Initial Load
    loadTasks();
    loadP2pStatus();

    // Background sync polling every 3s
    setInterval(() => {
        loadTasks();
        loadP2pStatus();
    }, 3000);
});
