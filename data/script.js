// DOM Elements
const tabBtns = document.querySelectorAll('.tab-btn');
const tabContents = document.querySelectorAll('.tab-content');

// Set Time Elements
const alarmForm = document.getElementById('alarm-form');
const alarmTimeInput = document.getElementById('alarm-time');
const alarmsListEl = document.getElementById('alarms-list');
const setTimeSaveBtn = document.getElementById('set-times-save-btn');

// Sync Time Elements
const syncTimeBtn = document.getElementById('sync-time-btn');

// Regular Interval Elements
const regIntervalHoursInput = document.getElementById('reg-interval-hours');
const regIntervalMinutesInput = document.getElementById('reg-interval-minutes');
const regIntervalSaveBtn = document.getElementById('reg-interval-save-btn');

// Random Interval Elements
const randIntervalHoursInput = document.getElementById('rand-interval-hours');
const randIntervalMinutesInput = document.getElementById('rand-interval-minutes');
const randIntervalSaveBtn = document.getElementById('rand-interval-save-btn');

// Settings Elements
const settingsForm = document.getElementById('settings-form');
const timeFormatSelect = document.getElementById('time-format');
const themeSelect = document.getElementById('theme');
const wifiSSIDInput = document.getElementById('wifi-ssid');
const wifiPasswordInput = document.getElementById('wifi-password');
const wifiShowPasswordBtn = document.getElementById('wifi-show-password');

// Notification Elements
const notificationEl = document.getElementById('notification');
const notificationMessageEl = document.getElementById('notification-message');
const notificationCloseBtn = document.getElementById('notification-close');

// History Elements
const historyListEl = document.getElementById('history-list');
const clearHistoryBtn = document.getElementById('clear-history-btn');
const refreshHistoryBtn = document.getElementById('refresh-history-btn');

let eventHistory = [];
let eventStats = null;

let alarms = [];
let settings = { timeFormat: '24', theme: 'light' };
let timeUpdateInterval = null;

// Modes enum (different mode types)
const MODES = Object.freeze({
    SET_TIMES: 'set_times',
    REGULAR_INTERVAL: 'regular_interval',
    RANDOM_INTERVAL: 'random_interval'
})

// ----------------------
// API Helper Functions
// ----------------------
async function apiGet(path) {
    try {
        const res = await fetch(path);
        return await res.json();
    } catch (error) {
        console.error('API GET error:', error);
        return null;
    }
}

async function apiPost(path, data) {
    try {
        const res = await fetch(path, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(data)
        });
        return await res.json();
    } catch (error) {
        console.error('API POST error:', error);
        return null;
    }
}

async function apiPatch(path) {
    try {
        const res = await fetch(path, { method: "PATCH" });
        return await res.json();
    } catch (error) {
        console.error('API PATCH error:', error);
        return null;
    }
}

async function apiDelete(path) {
    try {
        const res = await fetch(path, { method: "DELETE" });
        return await res.json();
    } catch (error) {
        console.error('API DELETE error:', error);
        return null;
    }
}

// ----------------------
// Load System Clock Time
// ----------------------
async function updateCurrentTime() {
    try {
        const res = await fetch('/api/time');
        const data = await res.json();
        
        if (data) {
            const hours = String(data.hour).padStart(2, '0');
            const minutes = String(data.minute).padStart(2, '0');
            const seconds = String(data.second).padStart(2, '0');
            
            let timeString;
            if (settings.timeFormat === '12') {
                const hour12 = data.hour % 12 || 12;
                const ampm = data.hour >= 12 ? 'PM' : 'AM';
                timeString = `${String(hour12).padStart(2, '0')}:${minutes}:${seconds} ${ampm}`;
            } else {
                timeString = `${hours}:${minutes}:${seconds}`;
            }
            
            document.getElementById('current-time').textContent = timeString;
        }
    } catch (error) {
        console.error('Error fetching time:', error);
        document.getElementById('current-time').textContent = 'Error';
    }
}

function startTimeUpdates() {
    // Update immediately
    updateCurrentTime();
    
    // Update every second
    if (timeUpdateInterval) {
        clearInterval(timeUpdateInterval);
    }
    timeUpdateInterval = setInterval(updateCurrentTime, 1000);
}

// ----------------------
// Load settings
// ----------------------
async function loadSettings() {
    const data = await apiGet("/api/settings");
    if (data) {
        settings = data;
        timeFormatSelect.value = settings.timeFormat || '24';
        themeSelect.value = settings.theme || 'light';
        applyTheme(settings.theme || 'light');

        if (settings.theme.length == 0) {
            settings.theme = 'light';
        }
    }
}

// ----------------------
// Load event history
// ----------------------
async function loadEventHistory() {
    try {
        const data = await apiGet('/api/events');
        if (data) {
            eventHistory = data;
            renderEventHistory();
        }
    } catch (error) {
        console.error('Error loading event history:', error);
        showNotification('Failed to load event history');
    }
}

// ----------------------
// Load event statistics
// ----------------------
async function loadEventStats() {
    try {
        const data = await apiGet('/api/events/stats');
        if (data) {
            eventStats = data;
            updateStatsDisplay();
        }
    } catch (error) {
        console.error('Error loading event stats:', error);
    }
}

// ----------------------
// Load alarms
// ----------------------
async function loadAlarms() {
    const data = await apiGet("/api/alarms");
    if (data) {
        alarms = data;
        renderAlarms();
    }
}

// ----------------------
// Apply theme
// ----------------------
function applyTheme(theme) {
    document.body.className = '';
    document.body.classList.add(theme + '-theme');
}

// ----------------------
// Load WiFi settings
// ----------------------
async function loadWiFiSettings() {
    try {
        const data = await apiGet("/api/wifi");
        if (data) {
            wifiSSIDInput.value = data.ssid || '';
            wifiPasswordInput.value = data.password || '';
        }
    } catch (error) {
        console.error('Error loading WiFi settings:', error);
    }
}

// ----------------------
// Save WiFi settings
// ----------------------
async function saveWiFiSettings(ssid, password) {
    try {
        const res = await fetch('/api/wifi', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ ssid, password })
        });
        
        const data = await res.json();
        
        if (res.ok) {
            showNotification(data.message || 'WiFi settings saved. Changes will apply on next wake.');
            return true;
        } else {
            showNotification(data.error || 'Failed to save WiFi settings');
            return false;
        }
    } catch (error) {
        console.error('Error saving WiFi settings:', error);
        showNotification('Error saving WiFi settings');
        return false;
    }
}

// ----------------------
// Set up tabs
// ----------------------
function setupTabs() {
    tabBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.getAttribute('data-tab');
            
            // Remove active class from all buttons and contents
            tabBtns.forEach(b => b.classList.remove('active'));
            tabContents.forEach(c => c.classList.remove('active'));
            
            // Add active class to clicked button and corresponding content
            btn.classList.add('active');
            document.getElementById(tabId).classList.add('active');

            
        });
    });
}

// ----------------------
// Update statistics display
// ----------------------
function updateStatsDisplay() {
    if (!eventStats) return;
    
    const statsEl = document.getElementById('event-stats');
    if (statsEl) {
        statsEl.innerHTML = `
            <div class="stat-item">
                <span class="stat-label">Total Events:</span>
                <span class="stat-value">${eventStats.totalEvents}</span>
            </div>
            <div class="stat-item success">
                <span class="stat-label">Successful:</span>
                <span class="stat-value">${eventStats.successCount}</span>
            </div>
            <div class="stat-item error">
                <span class="stat-label">Errors:</span>
                <span class="stat-value">${eventStats.errorCount}</span>
            </div>
        `;
    }
}

// ----------------------
// Render event history
// ----------------------
function renderEventHistory() {
    historyListEl.innerHTML = '';
    
    if (eventHistory.length === 0) {
        historyListEl.innerHTML = `
            <div class="empty-history">
                <p>No events in the past 24 hours</p>
            </div>
        `;
        return;
    }
    
    eventHistory.forEach(event => {
        const eventEl = document.createElement('div');
        eventEl.className = `event-item ${event.type.toLowerCase()}`;
        
        // Format the mode name for display
        let modeDisplay = event.mode;
        if (event.mode === 'set_times') {
            modeDisplay = 'Set Times';
        } else if (event.mode === 'regular_interval') {
            modeDisplay = 'Regular Interval';
        } else if (event.mode === 'random_interval') {
            modeDisplay = 'Random Interval';
        } else if (event.mode === 'system') {
            modeDisplay = 'System';
        }
        
        eventEl.innerHTML = `
            <div class="event-header">
                <span class="event-type-badge ${event.type.toLowerCase()}">${event.type}</span>
                <span class="event-mode">${modeDisplay}</span>
            </div>
            <div class="event-timestamp">${event.timeStr}</div>
            <div class="event-message">${event.message}</div>
        `;
        
        historyListEl.appendChild(eventEl);
    });
}

// ----------------------
// Clear event history
// ----------------------
async function clearEventHistory() {
    if (!confirm('Are you sure you want to clear all event history? This cannot be undone.')) {
        return;
    }
    
    try {
        const res = await fetch('/api/events', {
            method: 'DELETE'
        });
        
        if (res.ok) {
            eventHistory = [];
            renderEventHistory();
            await loadEventStats();
            showNotification('Event history cleared');
        } else {
            showNotification('Failed to clear event history');
        }
    } catch (error) {
        console.error('Error clearing event history:', error);
        showNotification('Error clearing event history');
    }
}

// ----------------------
// Render alarms
// ----------------------
function renderAlarms() {
    alarmsListEl.innerHTML = '';
    
    if (alarms.length === 0) {
        alarmsListEl.innerHTML = '<p style="text-align: center; opacity: 0.7;">No times set</p>';
        return;
    }
    
    alarms.forEach(alarm => {
        const alarmEl = document.createElement('div');
        alarmEl.className = `alarm-item ${alarm.active ? 'active' : ''}`;
        alarmEl.innerHTML = `
            <div>
                <div class="alarm-time">${formatAlarmTime(alarm.time)}</div>
            </div>
            <div class="alarm-actions">
                <button class="toggle-btn ${alarm.active ? 'active' : ''}" data-id="${alarm.id}">
                    ${alarm.active ? 'ON' : 'OFF'}
                </button>
                <button class="delete-btn" data-id="${alarm.id}">
                    DELETE
                </button>
            </div>
        `;
        
        alarmsListEl.appendChild(alarmEl);
    });
    
    // Add event listeners to alarm actions
    document.querySelectorAll('.toggle-btn').forEach(btn => {
        btn.addEventListener('click', async (e) => {
            const id = parseInt(btn.getAttribute('data-id'));
            await toggleAlarm(id);
        });
    });
    
    document.querySelectorAll('.delete-btn').forEach(btn => {
        btn.addEventListener('click', async (e) => {
            const id = parseInt(btn.getAttribute('data-id'));
            await deleteAlarm(id);
        });
    });
}

// ----------------------
// Format alarm time for display
// ----------------------
function formatAlarmTime(timeString) {
    const [hours, minutes] = timeString.split(':');
    let hoursNum = parseInt(hours);
    
    if (settings.timeFormat === '12') {
        const ampm = hoursNum >= 12 ? 'PM' : 'AM';
        hoursNum = hoursNum % 12;
        hoursNum = hoursNum ? hoursNum : 12;
        return `${String(hoursNum).padStart(2, '0')}:${minutes} ${ampm}`;
    } else {
        return `${String(hoursNum).padStart(2, '0')}:${minutes}`;
    }
}

// ----------------------
// Toggle alarm
// ----------------------
async function toggleAlarm(id) {
    const result = await apiPatch(`/api/alarms/${id}`);
    if (result) {
        alarms = result;
        renderAlarms();
        const alarm = alarms.find(a => a.id === id);
        showNotification(`Alarm ${alarm.active ? 'enabled' : 'disabled'}`);
    }
}

// ----------------------
// Delete alarm
// ----------------------
async function deleteAlarm(id) {
    const result = await apiDelete(`/api/alarms/${id}`);
    if (result) {
        alarms = result;
        renderAlarms();
        showNotification('Alarm deleted');
    }
}

// ----------------------
// Show notification
// ----------------------
function showNotification(message, persistent = false) {
    notificationMessageEl.textContent = message;
    notificationEl.classList.add('show');
    
    if (!persistent) {
        setTimeout(hideNotification, 3000);
    }
}

// ----------------------
// Hide notification
// ----------------------
function hideNotification() {
    notificationEl.classList.remove('show');
}

// ----------------------
// Set up event listeners
// ----------------------
function setupEventListeners() {
    // Alarm form submission
    alarmForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        
        const time = alarmTimeInput.value;
        if (!time) {
            showNotification('Please select a time');
            return;
        }
        
        const result = await apiPost('/api/alarms', { time });
        if (result) {
            alarms = result;
            renderAlarms();
            alarmForm.reset();
            showNotification('Alarm added successfully');
        } else {
            showNotification('Failed to add alarm');
        }
    });
    
    // Settings form submission
    settingsForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        
        settings = {
            timeFormat: timeFormatSelect.value,
            theme: themeSelect.value,
        };
        
        const res = await fetch('/api/settings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(settings)
        });
        
        if (res.ok) {
            applyTheme(settings.theme);
            renderAlarms(); // Re-render to apply time format changes
            updateCurrentTime();
            showNotification('Settings saved successfully');
        } else {
            showNotification('Failed to save settings');
        }

        // Save WiFi settings
        const wifiSSID = wifiSSIDInput.value.trim();
        const wifiPassword = wifiPasswordInput.value;
        
        // Validate WiFi settings
        if (wifiSSID.length < 8 || wifiSSID.length > 32) {
            showNotification('SSID must be 8-32 characters');
            return;
        }
        
        if (wifiPassword.length < 8 || wifiPassword.length > 63) {
            showNotification('Password must be 8-63 characters');
            return;
        }
        
        const wifiSaved = await saveWiFiSettings(wifiSSID, wifiPassword);
        
        if (res.ok && wifiSaved) {
            showNotification('All settings saved successfully!');
        }

    });

    // WiFi show/hide password toggle
    if (wifiShowPasswordBtn) {
        wifiShowPasswordBtn.addEventListener('click', () => {
            const type = wifiPasswordInput.type === 'password' ? 'text' : 'password';
            wifiPasswordInput.type = type;
            wifiShowPasswordBtn.textContent = type === 'password' ? 'Show' : 'Hide';
        });
    }
    
    // Notification close button
    notificationCloseBtn.addEventListener('click', () => {
        hideNotification();
    });

    // Sync System Time button
    syncTimeBtn.addEventListener('click', syncTime);

    // Save active mode buttons
    setTimeSaveBtn.addEventListener('click', setModeToSetTimes);
    regIntervalSaveBtn.addEventListener('click', setModeToRegInterval);
    randIntervalSaveBtn.addEventListener('click', setModeToRandInterval);

    // Sleep now button
    const sleepNowBtn = document.getElementById('sleep-now-btn');
    if (sleepNowBtn) {
        sleepNowBtn.addEventListener('click', async () => {
            if (confirm('Enter sleep mode now? Device will wake at next scheduled time.')) {
                showNotification('Entering sleep mode...', true);
                await fetch('/api/sleep', { method: 'POST' });
                setTimeout(() => {
                    showNotification('Device is now sleeping. Disconnect from WiFi.', true);
                }, 2000);
            }
        });
    }

    if (clearHistoryBtn) {
        clearHistoryBtn.addEventListener('click', clearEventHistory);
    }
    
    // Refresh history button
    if (refreshHistoryBtn) {
        refreshHistoryBtn.addEventListener('click', async () => {
            showNotification('Refreshing history...');
            await loadEventHistory();
            await loadEventStats();
            showNotification('History refreshed');
        });
    }

}

function getAESTOffset() {
    const now = new Date();
    const sydneyTime = new Date(now.toLocaleString('en-US', { timeZone: 'Australia/Sydney' }));
    const utcTime = new Date(now.toLocaleString('en-US', { timeZone: 'UTC' }));
    return sydneyTime.getTime() - utcTime.getTime();
}

async function syncTime() {
    try {
        // Get current UTC time
        const utcNow = Date.now();
        
        // Get AEST offset (handles DST automatically)
        const offset = getAESTOffset();
        
        // Convert to AEST
        const aestTimestamp = utcNow + offset;
        
        // Determine timezone name
        const isDST = offset > (10 * 60 * 60 * 1000);
        const tzName = isDST ? 'AEDT (UTC+11)' : 'AEST (UTC+10)';
        
        // Create date object to verify
        const aestDate = new Date(aestTimestamp);
        
        console.log('=== Time Sync ===');
        console.log('UTC time:', new Date(utcNow).toISOString());
        console.log('AEST offset:', offset / (60 * 60 * 1000), 'hours');
        console.log('AEST time:', aestDate.toLocaleString('en-AU', { timeZone: 'Australia/Sydney' }));
        console.log('Timezone:', tzName);
        console.log('Sending timestamp:', aestTimestamp);
        
        const result = await apiPost('/api/sync-time', { timestamp: aestTimestamp });
        
        if (result && result.success) {
            showNotification(`System time synced to ${tzName}`);
            updateCurrentTime();
        } else {
            showNotification('Failed to sync time');
        }
    } catch (error) {
        console.error('Time sync error:', error);
        showNotification('Failed to sync time');
    }
    
}

async function setModeToSetTimes() {
    const result = await apiPost('/api/mode/set-times', {});
    if (result) {
        await loadModeStatus();
        showNotification('Mode Set To Set Times');
    }
}

async function setModeToRegInterval() {
    const hours = parseInt(regIntervalHoursInput.value) || 0;
    const minutes = parseInt(regIntervalMinutesInput.value) || 0;
    
    if (hours === 0 && minutes === 0) {
        showNotification('Please set an interval greater than 0');
        return;
    }
    
    const result = await apiPost('/api/mode/regular-interval', { 
        hours, 
        minutes 
    });
    
    if (result) {
        await loadModeStatus();
        showNotification('Mode Set To Regular Interval');
    }
}

async function setModeToRandInterval() {
    const hours = parseInt(randIntervalHoursInput.value) || 0;
    const minutes = parseInt(randIntervalMinutesInput.value) || 0;
    
    if (hours === 0 && minutes === 0) {
        showNotification('Please set an interval greater than 0');
        return;
    }
    
    const result = await apiPost('/api/mode/random-interval', { 
        hours, 
        minutes 
    });
    
    if (result) {
        await loadModeStatus();
        showNotification('Mode Set To Random Interval');
    }
}

// Add new function to load and display mode status
async function loadModeStatus() {
    const data = await apiGet('/api/mode');
    if (data) {
        // Update active mode display
        let modeDisplay = '';
        if (data.activeMode === 'set_times') {
            modeDisplay = 'Set Times';
        } else if (data.activeMode === 'regular_interval') {
            modeDisplay = `Regular Interval (${data.regIntervalHours}h ${data.regIntervalMinutes}m)`;
        } else if (data.activeMode === 'random_interval') {
            modeDisplay = `Random Interval (${data.randIntervalHours}h ${data.randIntervalMinutes}m)`;
        }
        
        document.getElementById('active-mode').textContent = modeDisplay;
        document.getElementById('next-activation-time').textContent = data.nextActivationTime || 'Not set';
        
        // Update input fields
        regIntervalHoursInput.value = data.regIntervalHours;
        regIntervalMinutesInput.value = data.regIntervalMinutes;
        randIntervalHoursInput.value = data.randIntervalHours;
        randIntervalMinutesInput.value = data.randIntervalMinutes;
    }
}

// ----------------------
// Initialize App
// ----------------------
async function init() {
    console.log('Initializing app...');
    
    // Load settings first
    await loadSettings();

    // Load WiFi Config
    await loadWiFiSettings();
    
    // Set up tabs
    setupTabs();
    
    // Load and render alarms
    await loadAlarms();

    // Load Mode Status
    await loadModeStatus();

    // Load event history
    await loadEventHistory();
    await loadEventStats();
    
    // Set up event listeners
    setupEventListeners();

    // Start System Clock
    syncTime();
    startTimeUpdates();

    // Refresh Mode Status every 60 seconds
    setInterval(loadModeStatus, 60000);

    // Refresh event history every 5 minutes
    setInterval(() => {
        loadEventHistory();
        loadEventStats();
    }, 300000);
    
    console.log('App initialized');
}

// Start the app when DOM is ready
document.addEventListener('DOMContentLoaded', init);