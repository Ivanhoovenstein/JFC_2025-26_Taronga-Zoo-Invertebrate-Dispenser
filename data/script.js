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

// Notification Elements
const notificationEl = document.getElementById('notification');
const notificationMessageEl = document.getElementById('notification-message');
const notificationCloseBtn = document.getElementById('notification-close');

let alarms = [];
let settings = { timeFormat: '24', theme: 'light' };
let timeUpdateInterval = null;

// Modes enum (different mode types)
const MODES = Object.freeze({
    SET_TIMES: 'set_times',
    REGULAR_INTERVAL: 'regular_interval',
    RANDOM_INTERVAL: 'random_inteval'
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

// async function updateCurrentTime() {
//     try {
//         const res = await fetch('/api/time');
//         const data = await res.json();
        
//         if (data) {
//             // Create Date object from RTC data (UTC)
//             const utcDate = new Date(Date.UTC(
//                 data.year || 2025,
//                 (data.month || 1) - 1, 
//                 data.day || 1,
//                 data.hour,
//                 data.minute,
//                 data.second
//             ));
            
//             // Display in AEST timezone
//             const aestTime = utcDate.toLocaleString('en-AU', {
//                 timeZone: 'Australia/Sydney',
//                 hour12: settings.timeFormat === '12',
//                 hour: '2-digit',
//                 minute: '2-digit',
//                 second: '2-digit'
//             });
            
//             document.getElementById('current-time').textContent = aestTime;
//         }
//     } catch (error) {
//         console.error('Error fetching time:', error);
//         document.getElementById('current-time').textContent = 'Error';
//     }
// }

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
    });
    
    // Notification close button
    notificationCloseBtn.addEventListener('click', () => {
        hideNotification();
    });

    syncTimeBtn.addEventListener('click', syncTime);

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
    
    // Set up tabs
    setupTabs();
    
    // Load and render alarms
    await loadAlarms();

    // Load Mode Status
    await loadModeStatus();
    
    // Set up event listeners
    setupEventListeners();

    // Start System Clock
    syncTime();
    startTimeUpdates();

    // Refresh Mode Status every 60 seconds
    setInterval(loadModeStatus, 60000);
    
    console.log('App initialized');
}

// Start the app when DOM is ready
document.addEventListener('DOMContentLoaded', init);