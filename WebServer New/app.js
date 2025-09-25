// Estado de la aplicación
let sensorData = {
    temperature: [],
    ph: [],
    timestamps: []
};

let sequenceRunning = false;
let currentSequence = null;
let sequences = [];
let runOpt = { id: null, loop: false, nowEpoch: null };

// Charts
let tempChart = null;
let phChart = null;

// Variables para control de aireación y llenado
let aireacionState = false;
let co2State = false;
let pumpState = false;
let fillingActive = false;
let currentVolumeValue = 0;
let targetVolumeValue = 0;

// Variables para control de pH
let phAutoControl = false;
let phLimit = 7.0;
let co2InjectionTime = 0;
let co2TimeRemaining = 0;
let co2InjectionActive = false;
let co2LoopMode = false;
let co2LoopInterval = 4;
let co2Timer = null;
let co2TimesPerDay = 0;
let co2InjectionsCompleted = 0;
let co2NextInjectionTime = null;
let co2BucleMode = false;
let co2ScheduleTimer = null;
let co2NextInjectionTimer = null;
let co2DailyStartTime = null;

// Configuración de actualización
const UPDATE_INTERVAL = 1000; // 1 segundo

// Variables para límites y alarmas
let tempLimits = { min: 18, max: 28 };
let alarmState = {
    active: false,
    tempHigh: false,
    tempLow: false,
    phLow: false,
    silenced: false,
    silenceTime: null
};

// Variable para límite de pH
let phLimitMin = 6.0;

let rtcDriftMs = 0;
let rtcTickTimer = null;
let rtcResyncTimer = null;

// Helpers para números seguros (aceptan número o "NaN" string)
function fmt(val, digits) {
  const n = Number(val);
  return Number.isFinite(n) ? n.toFixed(digits) : 'NaN';
}
function numOrNa(val, fallback = NaN) {
  const n = Number(val);
  return Number.isFinite(n) ? n : fallback;
}

let readingsTimer = null;

// Inicialización
document.addEventListener('DOMContentLoaded', function() {
    startRtcClock();
    initCharts();
    loadSequences();
    startDataUpdate();
    updateConnectionStatus(true);

    const volumeInput = document.getElementById('volumeInput');
    const volumeSlider = document.getElementById('volumeSlider');
    
    if (volumeInput && volumeSlider) {
        volumeInput.addEventListener('input', function() {
            volumeSlider.value = this.value;
        });
        
        volumeSlider.addEventListener('input', function() {
            volumeInput.value = this.value;
        });
    }

});

// Funciones de navegación
function showSection(section) {
    // Ocultar todas las secciones
    document.querySelectorAll('.section').forEach(s => {
        s.classList.remove('active');
    });
    
    // Ocultar todos los botones activos
    document.querySelectorAll('.nav-btn').forEach(b => {
        b.classList.remove('active');
    });
    
    // Mostrar sección seleccionada
    document.getElementById(section).classList.add('active');
    
    // Activar botón correspondiente
    event.target.classList.add('active');
}

function showLedTab(tab) {
    // Ocultar todos los contenidos
    document.querySelectorAll('.led-content').forEach(c => {
        c.classList.remove('active');
    });
    
    // Desactivar todos los tabs
    document.querySelectorAll('.tab-btn').forEach(b => {
        b.classList.remove('active');
    });
    
    // Mostrar contenido seleccionado
    document.getElementById(tab).classList.add('active');
    
    // Activar tab correspondiente
    event.target.classList.add('active');
}

// Actualización de estado de conexión
function updateConnectionStatus(connected) {
   const statusIndicator = document.getElementById('status');
   const statusText = document.getElementById('statusText');
   
   if (connected) {
       statusIndicator.classList.add('connected');
       statusText.textContent = 'Conectado';
   } else {
       statusIndicator.classList.remove('connected');
       statusText.textContent = 'Desconectado';
   }
}

// Inicialización de gráficos
function initCharts() {
   // Configuración común para los gráficos
   const commonOptions = {
       responsive: true,
       maintainAspectRatio: false,
       plugins: {
           legend: {
               display: false
           }
       },
       scales: {
           x: {
               title: {
                display: true,
                text: 'Tiempo'
                },
               grid: {
                   color: 'rgba(255, 255, 255, 0.1)'
               },
               ticks: {
                   color: '#94a3b8'
               }
           },
           y: {
               beginAtZero: true,
               grid: {
                   color: 'rgba(255, 255, 255, 0.1)'
               },
               ticks: {
                   color: '#94a3b8'
               }
           }
       }
   };

   // Gráfico de temperatura
   const tempCtx = document.getElementById('tempChart').getContext('2d');
   tempChart = new Chart(tempCtx, {
       type: 'line',
       data: {
           labels: [],
           datasets: [{
               label: 'Temperatura',
               data: [],
               borderColor: '#f59e0b',
               backgroundColor: 'rgba(245, 158, 11, 0.1)',
               tension: 0.4
           }]
       },
       options: {
           ...commonOptions,
           scales: {
               ...commonOptions.scales,
               y: {
                   ...commonOptions.scales.y,
                   min: 0,
                   max: 50,
                   title: {
                        display: true,
                        text: 'Temperatura (°C)'
                    }
               }
           }
       }
   });

   // Gráfico de pH
   const phCtx = document.getElementById('phChart').getContext('2d');
    phChart = new Chart(phCtx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'pH',
                    data: [],
                    borderColor: 'rgb(75, 192, 192)',
                    backgroundColor: 'rgba(75, 192, 192, 0.2)',
                    tension: 0.1
                },
                {
                    label: 'Límite Inferior',
                    data: [],
                    borderColor: 'rgba(255, 206, 86, 0.8)',
                    backgroundColor: 'transparent',
                    borderDash: [5, 5],
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: false
                },
                {
                    label: 'Límite Control',
                    data: [],
                    borderColor: 'rgba(54, 162, 235, 0.8)',
                    backgroundColor: 'transparent',
                    borderDash: [5, 5],
                    borderWidth: 2,
                    pointRadius: 0,
                    fill: false
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    labels: {
                        color: '#ffffff'
                    }
                },
                title: {
                    display: false
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    min: 0,
                    max: 14,
                    ticks: {
                        color: '#ffffff'
                    },
                    grid: {
                        color: 'rgba(255, 255, 255, 0.1)'
                    },
                    title: {
                        display: true,
                        text: 'pH',
                        color: '#ffffff'
                    }
                },
                x: {
                    ticks: {
                        color: '#ffffff'
                    },
                    grid: {
                        color: 'rgba(255, 255, 255, 0.1)'
                    },
                    title: {
                        display: true,
                        text: 'Tiempo',
                        color: '#ffffff'
                    }
                }
            }
        }
    });
}

// Actualización de datos de sensores
async function updateSensorData() {
  try {
    const response = await fetch(`/api/sensors`, { cache: 'no-store' });
    const data = await response.json();

    const t = numOrNa(data.temperature);
    const p = numOrNa(data.ph);
    const c = numOrNa(data.turbidity);

    // Valores visibles
    document.getElementById('tempValue').textContent = fmt(t, 1);
    document.getElementById('phValue').textContent = fmt(p, 2);
    document.getElementById('concValue').textContent = fmt(c, 1);

    // Gauges e indicadores (usa 0/7 como neutro si NaN)
    updateTempGauge(Number.isFinite(t) ? t : 0);
    updatePhIndicator(Number.isFinite(p) ? p : 7);
    updateConcGauge(Number.isFinite(c) ? c : 0);

    // Histórico para gráficos (Chart.js ignora NaN)
    const timestamp = new Date().toLocaleTimeString();
    sensorData.timestamps.push(timestamp);
    sensorData.temperature.push(Number.isFinite(t) ? t : NaN);
    sensorData.ph.push(Number.isFinite(p) ? p : NaN);

    if (sensorData.timestamps.length > 50) {
      sensorData.timestamps.shift();
      sensorData.temperature.shift();
      sensorData.ph.shift();
    }

    updateCharts();
    updateConnectionStatus(true);
  } catch (error) {
    console.error('Error actualizando sensores:', error);
    updateConnectionStatus(false);
  }
}

function updateTempGauge(temp) {
   const percentage = (temp / 50) * 100;
   const fill = document.querySelector('#tempGauge .gauge-fill');
   fill.style.width = percentage + '%';
}

function updatePhIndicator(ph) {
   const percentage = (ph / 14) * 100;
   const marker = document.getElementById('phMarker');
   marker.style.left = percentage + '%';
   
   // Cambiar color según pH
   const indicator = document.getElementById('phIndicator');
   if (ph < 6) {
       indicator.style.borderColor = '#ef4444'; // Ácido
   } else if (ph > 8) {
       indicator.style.borderColor = '#8b5cf6'; // Base
   } else {
       indicator.style.borderColor = '#10b981'; // Neutro
   }
}

function updateConcGauge(conc) {
   const percentage = (conc / 40) * 100;
   const fill = document.querySelector('#concGauge .gauge-fill');
   fill.style.width = percentage + '%';
}

function updateCharts() {
    // Actualizar gráfico de temperatura
    tempChart.data.labels = sensorData.timestamps;
    tempChart.data.datasets[0].data = sensorData.temperature;

    // Actualizar líneas de límites de temperatura
    const limitDataPoints = sensorData.timestamps.length;
    tempChart.data.datasets.forEach(dataset => {
        if (dataset.label.includes('Límite')) {
            const limitValue = dataset.label.includes('Mínimo') ? 
                tempLimits.min : tempLimits.max;
            dataset.data = Array(limitDataPoints).fill(limitValue);
        }
    });

    tempChart.update();
    
    // Actualizar gráfico de pH
    phChart.data.labels = sensorData.timestamps;
    phChart.data.datasets[0].data = sensorData.ph;
    
    // Actualizar líneas de límites de pH
    phChart.data.datasets[1].data = Array(limitDataPoints).fill(phLimitMin);
    phChart.data.datasets[2].data = Array(limitDataPoints).fill(phLimit);
    
    phChart.update();
}

// Función para mostrar modal de límites
function showTempLimits() {
    document.getElementById('tempLimitsModal').style.display = 'block';
    updateTempLimitsPreview();
}

function closeTempLimitsModal() {
    document.getElementById('tempLimitsModal').style.display = 'none';
}

// Ajustar límites de temperatura
function adjustTempLimit(type, delta) {
    const input = document.getElementById(type === 'min' ? 'tempMinInput' : 'tempMaxInput');
    const slider = document.getElementById(type === 'min' ? 'tempMinSlider' : 'tempMaxSlider');
    
    let newValue = parseFloat(input.value) + delta;
    newValue = Math.max(10, Math.min(30, newValue));
    
    // Validar que min < max
    if (type === 'min') {
        const maxValue = parseFloat(document.getElementById('tempMaxInput').value);
        if (newValue >= maxValue) {
            newValue = maxValue - 0.5;
        }
    } else {
        const minValue = parseFloat(document.getElementById('tempMinInput').value);
        if (newValue <= minValue) {
            newValue = minValue + 0.5;
        }
    }
    
    input.value = newValue;
    slider.value = newValue;
    updateTempLimitsPreview();
}

// Sincronizar sliders
document.addEventListener('DOMContentLoaded', function() {
    const tempMinInput = document.getElementById('tempMinInput');
    const tempMinSlider = document.getElementById('tempMinSlider');
    const tempMaxInput = document.getElementById('tempMaxInput');
    const tempMaxSlider = document.getElementById('tempMaxSlider');
    
    if (tempMinInput && tempMinSlider) {
        tempMinInput.addEventListener('input', function() {
            tempMinSlider.value = this.value;
            updateTempLimitsPreview();
        });
        
        tempMinSlider.addEventListener('input', function() {
            tempMinInput.value = this.value;
            updateTempLimitsPreview();
        });
    }
    
    if (tempMaxInput && tempMaxSlider) {
        tempMaxInput.addEventListener('input', function() {
            tempMaxSlider.value = this.value;
            updateTempLimitsPreview();
        });
        
        tempMaxSlider.addEventListener('input', function() {
            tempMaxInput.value = this.value;
            updateTempLimitsPreview();
        });
    }
});

// Actualizar vista previa
function updateTempLimitsPreview() {
    const min = parseFloat(document.getElementById('tempMinInput').value);
    const max = parseFloat(document.getElementById('tempMaxInput').value);
    
    const range = document.getElementById('tempRangePreview');
    const minLabel = document.getElementById('previewMin');
    const maxLabel = document.getElementById('previewMax');
    
    const minPos = ((min - 10) / 20) * 100;
    const maxPos = ((max - 10) / 20) * 100;
    
    range.style.left = minPos + '%';
    range.style.width = (maxPos - minPos) + '%';
    
    minLabel.style.left = minPos + '%';
    minLabel.textContent = min + '°C';
    
    maxLabel.style.left = maxPos + '%';
    maxLabel.textContent = max + '°C';
}

// Guardar límites
async function saveTempLimits() {
    const min = parseFloat(document.getElementById('tempMinInput').value);
    const max = parseFloat(document.getElementById('tempMaxInput').value);
    
    try {
        await fetch(`/api/temp/save/limits`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ min: min, max: max })
        });
        
        tempLimits.min = min;
        tempLimits.max = max;
        
        // Actualizar display
        document.getElementById('tempLimitMin').textContent = min.toFixed(1);
        document.getElementById('tempLimitMax').textContent = max.toFixed(1);
        
        // Actualizar gráfico
        updateChartLimits();
        
        closeTempLimitsModal();
    } catch (error) {
        console.error('Error guardando límites:', error);
    }
}

// Obtener límites actuales
async function getTempLimits() {
    try {
        const response = await fetch(`/api/temp/load/limits`);
        const data = await response.json();
        
        tempLimits.min = data.min;
        tempLimits.max = data.max;
        
        document.getElementById('tempLimitMin').textContent = data.min.toFixed(1);
        document.getElementById('tempLimitMax').textContent = data.max.toFixed(1);
        document.getElementById('tempMinInput').value = data.min;
        document.getElementById('tempMaxInput').value = data.max;
        document.getElementById('tempMinSlider').value = data.min;
        document.getElementById('tempMaxSlider').value = data.max;
        
        updateChartLimits();
    } catch (error) {
        console.error('Error obteniendo límites:', error);
    }
}

// Actualizar líneas de límites en el gráfico
function updateChartLimits() {
    if (tempChart) {
        // Remover datasets anteriores de límites si existen
        tempChart.data.datasets = tempChart.data.datasets.filter(ds => 
            !ds.label.includes('Límite')
        );
        
        // Agregar líneas de límites
        tempChart.data.datasets.push({
            label: 'Límite Mínimo',
            data: Array(tempChart.data.labels.length).fill(tempLimits.min),
            borderColor: '#3b82f6',
            borderDash: [5, 5],
            borderWidth: 2,
            fill: false,
            pointRadius: 0,
            tension: 0
        });
        
        tempChart.data.datasets.push({
            label: 'Límite Máximo',
            data: Array(tempChart.data.labels.length).fill(tempLimits.max),
            borderColor: '#ef4444',
            borderDash: [5, 5],
            borderWidth: 2,
            fill: false,
            pointRadius: 0,
            tension: 0
        });
        
        tempChart.update();
    }
}

// Verificar alarmas
async function checkAlarmStatus() {
    try {
        const response = await fetch(`/api/alarm/status`);
        const data = await response.json();
        
        // Si hay alarma y no está silenciada o ha pasado el tiempo de silencio
        if (data.active && (!alarmState.silenced || 
            (alarmState.silenceTime && Date.now() - alarmState.silenceTime > 60000))) {
            
            alarmState.active = true;
            alarmState.tempHigh = data.tempHigh;
            alarmState.tempLow = data.tempLow;
            alarmState.phLow = data.phLow;
            
            showAlarmPanel();
        } else if (!data.active) {
            hideAlarmPanel();
            alarmState.silenced = false;
        }
    } catch (error) {
        console.error('Error verificando alarmas:', error);
    }
}

// Mostrar panel de alarmas
function showAlarmPanel() {
    const panel = document.getElementById('alarmPanel');
    const reasons = document.getElementById('alarmReasons');
    
    reasons.innerHTML = '';
    
    if (alarmState.tempHigh) {
        reasons.innerHTML += '<div class="alarm-reason">⚠️ Temperatura Alta</div>';
    }
    if (alarmState.tempLow) {
        reasons.innerHTML += '<div class="alarm-reason">⚠️ Temperatura Baja</div>';
    }
    if (alarmState.phLow) {
        reasons.innerHTML += '<div class="alarm-reason">⚠️ pH Bajo</div>';
    }
    
    panel.style.display = 'block';
    panel.classList.add('active');
}

// Ocultar panel de alarmas
function hideAlarmPanel() {
    const panel = document.getElementById('alarmPanel');
    panel.classList.remove('active');
    setTimeout(() => {
        panel.style.display = 'none';
    }, 300);
}

// Cargar historial de alarmas
async function loadAlarmHistory() {
    try {
        const response = await fetch('/api/alarms/history');
        const alarms = await response.json();
        
        const tbody = document.getElementById('alarm-history-body');
        tbody.innerHTML = '';
        
        // Mostrar las alarmas más recientes primero
        alarms.reverse().forEach(alarm => {
            const row = tbody.insertRow();
            row.insertCell(0).textContent = alarm.timestamp;
            row.insertCell(1).textContent = alarm.type;
            row.insertCell(2).textContent = alarm.value.toFixed(2);
        });
        
        if (alarms.length === 0) {
            tbody.innerHTML = '<tr><td colspan="3" style="text-align: center;">No hay alarmas registradas</td></tr>';
        }
    } catch (error) {
        console.error('Error cargando historial:', error);
    }
}

// Actualizar estado de alarma
async function updateAlarmStatus() {
    try {
        const response = await fetch('/api/alarm/status');
        const status = await response.json();
        
        const statusDiv = document.getElementById('alarm-status');
        const silenceBtn = document.getElementById('silence-alarm-btn');
        
        if (status.active) {
            statusDiv.className = 'alarm-status active';
            statusDiv.innerHTML = '<i class="fas fa-exclamation-triangle"></i> ALARMA ACTIVA';
            silenceBtn.disabled = false;
        } else {
            statusDiv.className = 'alarm-status inactive';
            statusDiv.innerHTML = '<i class="fas fa-check-circle"></i> Sin alarmas activas';
            silenceBtn.disabled = true;
        }
    } catch (error) {
        console.error('Error actualizando estado:', error);
    }
}

// Silenciar alarma
async function silenceAlarm() {
    try {
        await fetch(`/api/alarm/silence`, { method: 'POST' });
        
        alarmState.silenced = true;
        alarmState.silenceTime = Date.now();
        updateAlarmStatus();
        showNotification('Alarma silenciada', 'success');

        hideAlarmPanel();
    } catch (error) {
        console.error('Error silenciando alarma:', error);
        showNotification('Error al silenciar alarma', 'error');
    }
}

// Borrar historial de alarmas
async function clearAlarmHistory() {
    if (confirm('¿Está seguro de borrar todo el historial de alarmas?')) {
        try {
            await fetch('/api/alarms/clear', { method: 'POST' });
            loadAlarmHistory();
            showNotification('Historial borrado', 'success');
        } catch (error) {
            console.error('Error borrando historial:', error);
            showNotification('Error al borrar historial', 'error');
        }
    }
}

function showNotification(message, type) {
    // Implementación simple de notificación
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    notification.textContent = message;
    notification.style.cssText = `
        position: fixed;
        top: 20px;
        right: 20px;
        padding: 15px 20px;
        background: ${type === 'success' ? '#44ff44' : '#ff4444'};
        color: white;
        border-radius: 5px;
        z-index: 1000;
    `;
    document.body.appendChild(notification);
    setTimeout(() => notification.remove(), 3000);
}

// Actualizar estado de alarma periódicamente cuando se está en esa sección
setInterval(() => {
    const alarms = document.getElementById('alarms-section');
    if (alarms && alarms.classList.contains('active')) {
        updateAlarmStatus();
    }
}, 2000);

// Control de LEDs
async function updateLed(color, value) {
   const colorMap = {
       'white': 'blanco',
       'red': 'rojo',
       'green': 'verde',
       'blue': 'azul'
   };
   
   try {
       await fetch(`/led/${colorMap[color]}/pwm/${value}`);
       
       // Actualizar UI
       document.getElementById(`${color}Intensity`).textContent = value;
       document.getElementById(`${color}Slider`).value = value;
       
       // Actualizar indicador visual
       const indicator = document.getElementById(`${color}Led`);
       if (value > 0) {
           indicator.classList.add('active');
           indicator.style.opacity = value / 100;
       } else {
           indicator.classList.remove('active');
           indicator.style.opacity = 0.3;
       }
   } catch (error) {
       console.error('Error actualizando LED:', error);
   }
}

async function setLed(color, value) {
   document.getElementById(`${color}Slider`).value = value;
   await updateLed(color, value);
}

async function allLedsOn() {
   await setLed('white', 100);
   await setLed('red', 100);
   await setLed('green', 100);
   await setLed('blue', 100);
}

async function allLedsOff() {
   await setLed('white', 0);
   await setLed('red', 0);
   await setLed('green', 0);
   await setLed('blue', 0);
}

// Actualizar estado de LEDs
async function updateLedStatus() {
   try {
       const response = await fetch(`/leds/status`);
       const data = await response.json();
       
       // Actualizar sliders e indicadores
       ['blanco', 'rojo', 'verde', 'azul'].forEach(color => {
           const englishColor = {
               'blanco': 'white',
               'rojo': 'red',
               'verde': 'green',
               'azul': 'blue'
           }[color];
           
           const intensity = data[color].intensity;
           document.getElementById(`${englishColor}Slider`).value = intensity;
           document.getElementById(`${englishColor}Intensity`).textContent = intensity;
           
           const indicator = document.getElementById(`${englishColor}Led`);
           if (intensity > 0) {
               indicator.classList.add('active');
               indicator.style.opacity = intensity / 100;
           } else {
               indicator.classList.remove('active');
               indicator.style.opacity = 0.3;
           }
       });
   } catch (error) {
       console.error('Error obteniendo estado de LEDs:', error);
   }
}

// Manejo de secuencias
async function loadSequences() {
   try {
       const response = await fetch(`/api/sequences`);
       sequences = await response.json();
       
       const container = document.getElementById('sequencesList');
       container.innerHTML = '';
       
       sequences.forEach((seq, index) => {
           const card = document.createElement('div');
           card.className = 'sequence-card' + (seq.configured ? ' configured' : '');
           card.innerHTML = `
               <h4>Secuencia ${seq.id + 1}</h4>
               <div class="status">${seq.configured ? `${seq.steps} pasos` : 'No configurada'}</div>
               <div class="sequence-actions">
                    ${seq.configured ? 
                        `<div class="sequence-buttons">
                            <button class="btn btn-primary" onclick="openRunOptions(${seq.id}, false)">
                            <span>▶</span> 1 vez
                            </button>
                            <button class="btn btn-success" onclick="openRunOptions(${seq.id}, true)">
                            <span>🔁</span> Bucle
                            </button>
                        </div>` : 
                        ''}
                   <button class="btn btn-secondary" onclick="configureSequence(${seq.id})">Configurar</button>
                   ${seq.configured ? 
                       `<button class="btn btn-danger" onclick="deleteSequence(${seq.id})">Borrar</button>` : 
                       ''}
               </div>
           `;
           container.appendChild(card);
       });
       
       // Actualizar estado de secuencia
       updateSequenceStatus();
   } catch (error) {
       console.error('Error cargando secuencias:', error);
   }
}

async function updateSequenceStatus() {
   try {
       const response = await fetch(`/api/sequence/status`);
       const data = await response.json();
       
       const statusText = document.getElementById('seqStatusText');
       const stopBtn = document.getElementById('stopSeqBtn');
       const cancelBtn = document.getElementById('cancelScheduleBtn');
       
       if (data.running) {
           sequenceRunning = true;
           const modeText = data.loopMode ? ' (Bucle)' : ' (1 vez)';
           statusText.innerHTML = `Ejecutando Secuencia ${data.sequenceId + 1} - Paso ${data.currentStep + 1}/${data.totalSteps} ${modeText}
                                  (${data.elapsedHours}h ${data.elapsedMinutes}m ${data.elapsedSeconds}s / 
                                   ${data.totalHours}h ${data.totalMinutes}m ${data.totalSeconds}s)`;
           stopBtn.style.display = 'inline-block';
           cancelBtn.style.display = 'none';
           return;
       } else {
           sequenceRunning = false;
           statusText.textContent = 'Inactivo';
           stopBtn.style.display = 'none';
       }

        if (data.scheduled) {
        const when = new Date((data.startEpoch || 0) * 1000);
        const modeText = data.loopMode ? ' (Bucle)' : ' (1 vez)';
        statusText.textContent =
            `Programada Secuencia ${data.sequenceId + 1}${modeText} – Inicio: ${when.toLocaleString()}`;
        stopBtn.style.display = 'none';
        cancelBtn.style.display = 'inline-block';
        } else {
        statusText.textContent = 'Inactivo';
        stopBtn.style.display = 'none';
        cancelBtn.style.display = 'none';
        }       

   } catch (error) {
       console.error('Error obteniendo estado de secuencia:', error);
   }
}

async function startSequence(id, loop = false) {
    try {
        // Detener cualquier secuencia en ejecución
        await fetch('/api/sequence/stop', { method: 'POST' });
        
        // Iniciar la nueva secuencia
        const endpoint = loop ? `/api/sequence/start/loop/${id}` : `/api/sequence/start/${id}`;
        const response = await fetch(endpoint, { method: 'POST' });
        
        if (response.ok) {
            alert(loop ? `Secuencia ${id + 1} iniciada en bucle` : `Secuencia ${id + 1} iniciada (1 vez)`);
            // Actualizar el estado de la secuencia
            updateSequenceStatus();
        } else {
            alert('Error al iniciar la secuencia');
        }
    } catch (error) {
        console.error('Error iniciando secuencia:', error);
        alert('Error de conexión');
    }
}

async function stopSequence() {
   try {
       await fetch(`/api/sequence/stop`, { method: 'POST' });
       loadSequences();
   } catch (error) {
       console.error('Error deteniendo secuencia:', error);
   }
}

async function configureSequence(id) {
  currentSequence = id;
  document.getElementById('seqNumber').textContent = id + 1;

  // 1) Traer la configuración actual
  let prefill = null;
  try {
    const res = await fetch(`/api/sequence/config/${id}`);
    if (res.ok) {
      const cfg = await res.json();
      if (cfg.configured && Array.isArray(cfg.steps) && cfg.steps.length > 0) {
        prefill = cfg.steps;   // [{colors:[w,r,g,b], hours, minutes, seconds}, ...]
      }
    }
  } catch (e) {
    console.error('Error cargando config de secuencia:', e);
  }

  // 2) Ajustar número de pasos y renderizar con datos existentes
  const stepCountInput = document.getElementById('stepCount');
  stepCountInput.value = prefill ? prefill.length : 1;

  updateStepsConfig(prefill);   // <<— pásale los pasos existentes

  // 3) Abrir modal
  document.getElementById('configModal').style.display = 'block';
}

function updateStepsConfig(prefillSteps = null) {
  const stepCount = parseInt(document.getElementById('stepCount').value);
  const container = document.getElementById('stepsContainer');
  container.innerHTML = '';

  for (let i = 0; i < stepCount; i++) {
    const s = (prefillSteps && prefillSteps[i]) ? prefillSteps[i] : null;
    const w = s?.colors?.[0] ?? 0;
    const r = s?.colors?.[1] ?? 0;
    const g = s?.colors?.[2] ?? 0;
    const b = s?.colors?.[3] ?? 0;
    const h = s?.hours ?? 0;
    const m = s?.minutes ?? 0;
    const sec = s?.seconds ?? 0;

    const stepDiv = document.createElement('div');
    stepDiv.className = 'step-config';
    stepDiv.innerHTML = `
      <h4>Paso ${i + 1}</h4>
      <div class="color-controls">
        <div class="color-control">
          <label>Blanco:</label>
          <input type="range" id="step${i}_white" min="0" max="100" value="${w}">
          <span>${w}</span>
        </div>
        <div class="color-control">
          <label>Rojo:</label>
          <input type="range" id="step${i}_red"   min="0" max="100" value="${r}">
          <span>${r}</span>
        </div>
        <div class="color-control">
          <label>Verde:</label>
          <input type="range" id="step${i}_green" min="0" max="100" value="${g}">
          <span>${g}</span>
        </div>
        <div class="color-control">
          <label>Azul:</label>
          <input type="range" id="step${i}_blue"  min="0" max="100" value="${b}">
          <span>${b}</span>
        </div>
      </div>
      <div class="time-controls">
        <div class="time-control">
          <label>Horas:</label>
          <input type="number" id="step${i}_hours"   min="0" max="23" value="${h}">
        </div>
        <div class="time-control">
          <label>Minutos:</label>
          <input type="number" id="step${i}_minutes" min="0" max="59" value="${m}">
        </div>
        <div class="time-control">
          <label>Segundos:</label>
          <input type="number" id="step${i}_seconds" min="0" max="59" value="${sec}">
        </div>
      </div>
    `;
    container.appendChild(stepDiv);

    // Listeners para reflejar el valor junto al slider
    stepDiv.querySelectorAll('input[type="range"]').forEach(input => {
      input.addEventListener('input', function () {
        this.nextElementSibling.textContent = this.value;
      });
    });
  }
}

async function saveSequence() {
   const stepCount = parseInt(document.getElementById('stepCount').value);
   const steps = [];
   
   for (let i = 0; i < stepCount; i++) {
       steps.push({
           colors: [
               parseInt(document.getElementById(`step${i}_white`).value),
               parseInt(document.getElementById(`step${i}_red`).value),
               parseInt(document.getElementById(`step${i}_green`).value),
               parseInt(document.getElementById(`step${i}_blue`).value)
           ],
           hours: parseInt(document.getElementById(`step${i}_hours`).value),
           minutes: parseInt(document.getElementById(`step${i}_minutes`).value),
           seconds: parseInt(document.getElementById(`step${i}_seconds`).value)
       });
   }
   
   const sequenceData = {
       id: currentSequence,
       steps: steps
   };
   
   try {
       await fetch(`/api/sequence/save`, {
           method: 'POST',
           headers: {
               'Content-Type': 'application/json'
           },
           body: JSON.stringify(sequenceData)
       });
       
       closeModal();
       loadSequences();
   } catch (error) {
       console.error('Error guardando secuencia:', error);
   }
}

async function deleteSequence(id) {
   if (confirm(`¿Estás seguro de borrar la Secuencia ${id + 1}?`)) {
       try {
           // Aquí deberías implementar el endpoint de borrado en el servidor
           await fetch(`/api/sequence/delete/${id}`, { method: 'DELETE' });
           loadSequences();
       } catch (error) {
           console.error('Error borrando secuencia:', error);
       }
   }
}

// Control de Aireación
async function controlAireacion(state) {
    try {
        const endpoint = state ? '/api/aireacion/on' : '/api/aireacion/off';
        await fetch(`${endpoint}`);
        updateAireacionStatus();
    } catch (error) {
        console.error('Error controlando aireación:', error);
    }
}

// Control de CO2
async function controlCO2(state) {
    try {
        const endpoint = state ? '/api/co2/on' : '/api/co2/off';
        await fetch(`${endpoint}`);
        updateAireacionStatus();
    } catch (error) {
        console.error('Error controlando CO2:', error);
    }
}

// Actualizar estado de aireación y CO2
async function updateAireacionStatus() {
    try {
        const response = await fetch(`/api/aireacion/status`);
        const data = await response.json();
        
        // Actualizar indicadores de aireación
        const aireacionIndicator = document.getElementById('aireacionIndicator');
        const aireacionStatus = document.getElementById('aireacionStatus');
        
        if (data.aireacion) {
            aireacionIndicator.classList.add('active');
            aireacionStatus.textContent = 'ENCENDIDO';
            aireacionState = true;
        } else {
            aireacionIndicator.classList.remove('active');
            aireacionStatus.textContent = 'APAGADO';
            aireacionState = false;
        }
        
        // Actualizar indicadores de CO2
        const co2Indicator = document.getElementById('co2Indicator');
        const co2Status = document.getElementById('co2Status');
        
        if (data.co2) {
            co2Indicator.classList.add('active-co2');
            co2Status.textContent = 'ENCENDIDO';
            co2State = true;
        } else {
            co2Indicator.classList.remove('active-co2');
            co2Status.textContent = 'APAGADO';
            co2State = false;
        }
    } catch (error) {
        console.error('Error obteniendo estado de aireación:', error);
    }
}

// Control de Llenado
async function updateFillingStatus() {
    try {
        const response = await fetch(`/api/llenado/status`);
        const data = await response.json();
        
        // Actualizar volumen actual
        currentVolumeValue = data.volumeTotal;
        document.getElementById('currentVolume').textContent = currentVolumeValue.toFixed(1);
        
        // Actualizar gauge de volumen
        const percentage = (currentVolumeValue / 200) * 100;
        document.getElementById('volumeFill').style.height = percentage + '%';
        
        // Actualizar estado de llenado
        const fillingStatusText = document.getElementById('fillingStatusText');
        const fillingProgress = document.getElementById('fillingProgress');
        const pumpIndicator = document.getElementById('pumpIndicator');
        const pumpStatus = document.getElementById('pumpStatus');
        
        const manualStatsBlock  = document.getElementById('manualStatsBlock');
        const pulseCountUI      = document.getElementById('pulseCountUI');
        const manualVolumeUI    = document.getElementById('manualVolumeUI');

        if (data.fillingActive) {
            fillingActive = true;
            pumpIndicator.classList.add('active');
            pumpStatus.textContent = 'Bomba ENCENDIDA';

            if (!data.isManualMode) {
                fillingStatusText.textContent = 'Llenando...';
                fillingProgress.style.display = 'block';
                
                document.getElementById('fillingVolume').textContent = data.volumeLlenado.toFixed(1);
                document.getElementById('targetVolume').textContent = data.targetVolume.toFixed(0);
                document.getElementById('startFillBtn').style.display = 'none';
                document.getElementById('stopFillBtn').style.display = 'inline-block';

                const progress = (data.volumeLlenado / data.targetVolume) * 100;
                document.getElementById('progressFill').style.width = progress + '%';
            
                if (manualStats) manualStats.style.display = 'none';
            } else {
                fillingStatusText.textContent = 'Bomba Manual Activa';
                fillingProgress.style.display = 'none';
                document.getElementById('startFillBtn').style.display = 'inline-block';
                document.getElementById('stopFillBtn').style.display = 'none';

                if (manualStatsBlock) {
                manualStatsBlock.style.display = 'block';
                if (pulseCountUI)   pulseCountUI.textContent   = `Pulsos: ${Number(data.pulsos || 0)}`;
                if (manualVolumeUI) manualVolumeUI.textContent =
                    `Llenado: ${Number(data.volumeLlenado || 0).toFixed(1)} L`;
                }
            }
        } else {
            fillingActive = false;
            pumpIndicator.classList.remove('active');
            pumpStatus.textContent = 'Bomba APAGADA';
            fillingStatusText.textContent = 'Inactivo';
            fillingProgress.style.display = 'none';

            if (manualStatsBlock) manualStatsBlock.style.display = 'none';
        }
    } catch (error) {
        console.error('Error obteniendo estado de llenado:', error);
    }
}

// Iniciar llenado automático
async function startAutoFill() {
    const volume = document.getElementById('volumeInput').value;
    
    if (volume <= 0 || volume > 200) {
        alert('Por favor ingrese un volumen válido entre 1 y 200 litros');
        return;
    }
    
    try {
        await fetch(`/api/llenado/start`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: `volume=${volume}`
        });
        
        document.getElementById('startFillBtn').style.display = 'none';
        document.getElementById('stopFillBtn').style.display = 'inline-block';    

        updateFillingStatus();
    } catch (error) {
        console.error('Error iniciando llenado:', error);
    }
}

// Control manual de bomba
async function controlPump(state) {
    try {
        if (state) {
            await fetch(`/api/llenado/manual/start`, { method: 'POST' });
        } else {
            await fetch(`/api/llenado/stop`, { method: 'POST' });
        }
        updateFillingStatus();
    } catch (error) {
        console.error('Error controlando bomba:', error);
    }
}

// Reiniciar volumen
async function resetVolume() {
    if (confirm('¿Está seguro de reiniciar el volumen a 0?')) {
        try {
            await fetch(`/api/llenado/reset`, { method: 'POST' });
            updateFillingStatus();
        } catch (error) {
            console.error('Error reiniciando volumen:', error);
        }
    }
}

// Mostrar control de pH
function showPhControl() {
    document.querySelectorAll('.section').forEach(s => {
        s.classList.remove('active');
    });
    document.getElementById('phControl').classList.add('active');
    updatePhControlDisplay();
}

// Actualizar display de control de pH
function updatePhControlDisplay() {
    const phValue = parseFloat(document.getElementById('phValue').textContent) || 7.0;
    document.getElementById('phCurrentLarge').textContent = phValue.toFixed(1);
    
    // Actualizar indicador de estado
    const statusBar = document.querySelector('.ph-status-bar');
    const statusText = document.getElementById('phStatusText');
    
    if (phValue < phLimit - 0.2) {
        statusBar.className = 'ph-status-bar acid';
        statusText.textContent = 'ÁCIDO';
    } else if (phValue > phLimit + 0.2) {
        statusBar.className = 'ph-status-bar alkaline';
        statusText.textContent = 'ALCALINO';
    } else {
        statusBar.className = 'ph-status-bar neutral';
        statusText.textContent = 'EQUILIBRADO';
    }
    
    document.getElementById('phFixedValue').textContent = phLimit.toFixed(1);
}

// Ajustar límite de pH
function adjustPhLimit(delta) {
    phLimit = Math.max(0, Math.min(14, phLimit + delta));
    document.getElementById('phLimitInput').value = phLimit.toFixed(1);
    savePhLimitSet();
    updatePhControlDisplay();
}

// Activar control automático de pH
async function setPhAutoControl() {
    phLimit = parseFloat(document.getElementById('phLimitInput').value);
    
    try {
        await fetch(`/api/ph/auto/set`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ limit: phLimit })
        });
        
        phAutoControl = true;
        document.getElementById('autoControlStatus').textContent = 'ACTIVO';
        document.getElementById('co2AutoLight').classList.add('active');
        
        updateCharts();

        console.log(`Control automático activado. pH límite: ${phLimit}`);
    } catch (error) {
        console.error('Error activando control automático:', error);
    }
}

// Desactivar control automático
async function stopPhAutoControl() {
    try {
        await fetch(`/api/ph/auto/stop`, { method: 'POST' });
        
        phAutoControl = false;
        document.getElementById('autoControlStatus').textContent = 'INACTIVO';
        document.getElementById('co2AutoLight').classList.remove('active');
        
        console.log('Control automático desactivado');
    } catch (error) {
        console.error('Error desactivando control automático:', error);
    }
}

// Ajustar tiempo de CO2
function adjustCO2Time(unit, delta) {
    if (unit === 'minutes') {
        let minutes = parseInt(document.getElementById('co2Minutes').value) || 0;
        minutes = Math.max(0, Math.min(59, minutes + delta));
        document.getElementById('co2Minutes').value = minutes;
    } else if (unit === 'times') {
        let times = parseInt(document.getElementById('co2Times').value) || 0;
        times = Math.max(0, Math.min(24, times + delta));
        document.getElementById('co2Times').value = times;
    }
}

function toggleCO2Bucle() {
    co2BucleMode = !co2BucleMode;
    const btn = document.getElementById('co2BucleBtn');
    if (co2BucleMode) {
        btn.textContent = 'Modo Bucle: ACTIVADO';
        btn.classList.add('active');
    } else {
        btn.textContent = 'Modo Bucle: DESACTIVADO';
        btn.classList.remove('active');
    }
}

// Iniciar inyección de CO2
async function startCO2Injection() {
    if (co2InjectionActive) {
        alert('Ya hay una inyección en progreso');
        return;
    }
    
    const minutes = parseInt(document.getElementById('co2Minutes').value) || 0;
    const times = parseInt(document.getElementById('co2Times').value) || 0;
    
    if (minutes === 0) {
        alert('Por favor configure el tiempo de inyección');
        return;
    }
    
    co2InjectionTime = minutes * 60;
    co2TimesPerDay = times;
    co2InjectionsCompleted = 0;
    co2DailyStartTime = new Date();
    
    // Actualizar indicadores
    document.getElementById('co2InjectionTotal').textContent = times || 1;
    
    try {
        await fetch(`/api/co2/manual/start`, {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({ 
                minutes: minutes,
                times: times,
                bucle: co2BucleMode
            })
        });
        
        if (times > 0) {
            // Programar inyecciones múltiples
            scheduleCO2Injections();
        } else {
            // Inyección única
            executeCO2Injection();
        }
        
    } catch (error) {
        console.error('Error iniciando inyección:', error);
    }
}

function scheduleCO2Injections() {
    const intervalHours = 24 / co2TimesPerDay;
    const intervalMs = intervalHours * 3600000;
    
    // Ejecutar primera inyección
    executeCO2Injection();
    
    // Programar siguientes inyecciones
    co2ScheduleTimer = setInterval(() => {
        if (co2InjectionsCompleted < co2TimesPerDay) {
            executeCO2Injection();
        } else if (co2BucleMode) {
            // Reiniciar ciclo en modo bucle
            co2InjectionsCompleted = 0;
            co2DailyStartTime = new Date();
            document.getElementById('co2InjectionCount').textContent = 0;
        } else {
            // Detener después de 24 horas
            stopCO2Injection();
        }
    }, intervalMs);
    
    // Actualizar contador de próxima inyección
    updateNextInjectionTimer(intervalMs);
}

function executeCO2Injection() {
    co2InjectionActive = true;
    co2TimeRemaining = co2InjectionTime;
    co2InjectionsCompleted++;
    
    document.getElementById('co2InjectionCount').textContent = co2InjectionsCompleted;
    
    startCO2Timer();
}

function updateNextInjectionTimer(intervalMs) {
    if (co2NextInjectionTimer) clearInterval(co2NextInjectionTimer);
    
    let nextTime = intervalMs;
    
    co2NextInjectionTimer = setInterval(() => {
        nextTime -= 1000;
        
        if (nextTime > 0 && !co2InjectionActive) {
            const hours = Math.floor(nextTime / 3600000);
            const minutes = Math.floor((nextTime % 3600000) / 60000);
            const seconds = Math.floor((nextTime % 60000) / 1000);
            
            document.getElementById('co2NextInjection').textContent = 
                `${hours.toString().padStart(2,'0')}:${minutes.toString().padStart(2,'0')}:${seconds.toString().padStart(2,'0')}`;
        } else {
            document.getElementById('co2NextInjection').textContent = '--:--:--';
        }
    }, 1000);
}

// Timer de CO2
function startCO2Timer() {
    
    if (co2Timer) {
        clearInterval(co2Timer);
        co2Timer = null;
    }
    
    co2Timer = setInterval(() => {
        if (co2TimeRemaining > 0) {
            co2TimeRemaining--;
            updateCO2TimerDisplay();
        } else {
            // Tiempo expirado
            co2InjectionActive = false;
            clearInterval(co2Timer);
            document.getElementById('timerDisplay').classList.add('expired');
            
            // Notificar al servidor
            fetch('/api/co2/manual/complete', {method: 'POST'});
            
            setTimeout(() => {
                document.getElementById('timerDisplay').classList.remove('expired');
            }, 2000);
        }
    }, 1000);
}

// Actualizar display del timer
function updateCO2TimerDisplay() {
    const minutes = Math.floor(co2TimeRemaining / 60);
    const seconds = co2TimeRemaining % 60;
    
    const display = `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
    document.getElementById('co2TimeRemaining').textContent = display;
    
    const progress = (co2TimeRemaining / co2InjectionTime) * 100;
    document.getElementById('timerFill').style.width = progress + '%';
    
    if (co2TimeRemaining === 0) {
        document.getElementById('co2TimeRemaining').style.color = 'var(--danger-color)';
    }
}

// Pausar inyección
async function stopCO2Injection() {
    try {
        await fetch(`/api/co2/manual/stop`, { method: 'POST' });
        
        // Limpiar todos los timers
        if (co2Timer) clearInterval(co2Timer);
        if (co2ScheduleTimer) clearInterval(co2ScheduleTimer);
        if (co2NextInjectionTimer) clearInterval(co2NextInjectionTimer);
        
        co2InjectionActive = false;
        co2TimeRemaining = 0;
        
        document.getElementById('co2NextInjection').textContent = '--:--:--';
        updateCO2TimerDisplay();
        
        console.log('Inyección detenida');
    } catch (error) {
        console.error('Error deteniendo inyección:', error);
    }
}

// Reiniciar inyección
async function resetCO2Injection() {
    try {
        await fetch(`/api/co2/manual/reset`, { method: 'POST' });
        
        // Detener todo
        await stopCO2Injection();
        
        // Resetear valores
        document.getElementById('co2Minutes').value = 0;
        document.getElementById('co2Times').value = 0;
        document.getElementById('co2InjectionCount').textContent = 0;
        document.getElementById('co2InjectionTotal').textContent = 0;
        co2BucleMode = false;
        document.getElementById('co2BucleBtn').textContent = 'Modo Bucle: DESACTIVADO';
        document.getElementById('co2BucleBtn').classList.remove('active');
        
        console.log('Valores reiniciados');
    } catch (error) {
        console.error('Error reiniciando:', error);
    }
}

// Detener llenado automático
async function stopFilling() {
    try {
        await fetch(`/api/llenado/stop`, { method: 'POST' });
        
        document.getElementById('startFillBtn').style.display = 'inline-block';
        document.getElementById('stopFillBtn').style.display = 'none';
        
        updateFillingStatus();
    } catch (error) {
        console.error('Error deteniendo llenado:', error);
    }
}

async function updatePhStatus() {
    if (document.getElementById('phControl').classList.contains('active')) {
        try {
            const response = await fetch(`/api/ph/status`);
            const data = await response.json();
            phLimit = data.phLimit || 7.0;

            const phVal = numOrNa(data.phValue);
            document.getElementById('phCurrentLarge').textContent = fmt(phVal, 1);
            document.getElementById('phFixedValue').textContent = data.phLimit.toFixed(1);
            
            if (data.autoControl) {
                document.getElementById('autoControlStatus').textContent = 'ACTIVO';
                document.getElementById('co2AutoLight').classList.add('active');
            } else {
                document.getElementById('autoControlStatus').textContent = 'INACTIVO';
                document.getElementById('co2AutoLight').classList.remove('active');
            }
            
            if (data.co2InjectionActive) {
                co2TimeRemaining = data.co2MinutesRemaining * 60;
                updateCO2TimerDisplay();
            }
            
            updatePhControlDisplay();
            updateCharts();

        } catch (error) {
            console.error('Error actualizando estado pH:', error);
        }
    }
}

// Cargar límite de pH al iniciar
async function loadPhLimit() {
    try {
        const response = await fetch('/api/ph/load/limit');
        const data = await response.json();
        phLimitMin = data.min;
        document.getElementById('phMinInput').value = phLimitMin;
        document.getElementById('phMinSlider').value = phLimitMin;
        updatePhLimitPreview();
        
        // Actualizar gráfico con nuevo límite
        updateCharts();
    } catch (error) {
        console.error('Error cargando límite de pH:', error);
    }
}

// Mostrar modal de límite de pH
function showPhLimits() {
    document.getElementById('phLimitModal').style.display = 'block';
    updatePhLimitPreview();
}

// Cerrar modal de pH
function closePhLimitModal() {
    document.getElementById('phLimitModal').style.display = 'none';
}

// Ajustar límite mínimo de pH
function adjustPhLimitMin(delta) {
    const input = document.getElementById('phMinInput');
    const slider = document.getElementById('phMinSlider');
    
    let newValue = parseFloat(input.value) + delta;
    newValue = Math.max(0, Math.min(14, newValue));
    
    input.value = newValue.toFixed(1);
    slider.value = newValue;
    phLimitMin = newValue;
    
    updatePhLimitPreview();
}

// Actualizar preview del límite
function updatePhLimitPreview() {
    const percentage = (phLimitMin / 14) * 100;
    document.getElementById('phLimitMarker').style.left = percentage + '%';
    document.getElementById('phLimitPreview').textContent = phLimitMin.toFixed(1);
    
    // Actualizar posición del marcador actual
    const currentPh = parseFloat(document.getElementById('phValue').textContent) || 7.0;
    const currentPercentage = (currentPh / 14) * 100;
    document.getElementById('phCurrentMarker').style.left = currentPercentage + '%';
    document.getElementById('phCurrentPreview').textContent = currentPh.toFixed(1);
}

async function savePhLimitSet() {
    phLimit = parseFloat(document.getElementById('phLimitInput').value);
    
    try {
        await fetch('/api/ph/set/control_limit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ controlLimit: phLimit })
        });
        showNotification('Límite de pH Fijado guardado', 'success');
    } catch (error) {
        console.error('Error guardando pH Fijado:', error);
        showNotification('Error al guardar pH Fijado', 'error');
    }
}

// Guardar límite de pH
async function savePhLimit() {
    phLimitMin = parseFloat(document.getElementById('phMinInput').value);
    
    try {
        await fetch('/api/ph/save/limit', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ min: phLimitMin })
        });
        
        closePhLimitModal();
        showNotification('Límite de pH guardado', 'success');
        updateCharts();
    } catch (error) {
        console.error('Error guardando límite:', error);
        showNotification('Error al guardar límite', 'error');
    }
}

// Sincronizar input y slider de pH
document.getElementById('phMinInput')?.addEventListener('input', function() {
    document.getElementById('phMinSlider').value = this.value;
    phLimitMin = parseFloat(this.value);
    updatePhLimitPreview();
});

document.getElementById('phMinSlider')?.addEventListener('input', function() {
    document.getElementById('phMinInput').value = this.value;
    phLimitMin = parseFloat(this.value);
    updatePhLimitPreview();
});

async function openRunOptions(id, loop) {
  runOpt.id = id;
  runOpt.loop = !!loop;

  // Trae hora del RTC para min y valor por defecto
  try {
    const res = await fetch('/api/time/now', { cache: 'no-store' });
    const t = await res.json();
    runOpt.nowEpoch = t.epoch;

    const dt = document.getElementById('scheduleDateTime');
    dt.min = t.iso;                             // "YYYY-MM-DDTHH:MM"
    dt.value = t.iso;                           // por defecto ahora (ajusta a gusto +1 min)
    document.getElementById('scheduleHint').textContent =
      'La hora se basa en el RTC del equipo.';
  } catch (e) {
    console.warn('No se pudo leer RTC, uso hora local del navegador');
    const dt = document.getElementById('scheduleDateTime');
    const now = new Date();
    const iso = now.toISOString().slice(0,16); // YYYY-MM-DDTHH:MM (UTC)
    dt.min = iso; dt.value = iso;
  }

  document.getElementById('schedulePanel').style.display = 'none';
  document.getElementById('runOptionsModal').style.display = 'block';

  // Botones
  document.getElementById('runNowBtn').onclick = () => {
    closeRunOptions();
    startSequence(runOpt.id, runOpt.loop);  // tu función existente
  };
  document.getElementById('openScheduleBtn').onclick = () => {
    document.getElementById('schedulePanel').style.display = 'block';
  };
}

function closeRunOptions() {
  document.getElementById('runOptionsModal').style.display = 'none';
}

async function confirmSchedule() {
  const dt = document.getElementById('scheduleDateTime').value; // "YYYY-MM-DDTHH:MM(:SS?)"
  if (!dt) { alert('Selecciona fecha y hora'); return; }

  // Convierte a epoch (segundos). Asumimos hora local del navegador.
  const epoch = Math.floor(new Date(dt).getTime() / 1000);

  // Valida contra el RTC (si lo tenemos)
  if (runOpt.nowEpoch && epoch <= runOpt.nowEpoch) {
    alert('La hora debe ser posterior a la actual del equipo.');
    return;
  }

  // Enviar programación
  const body = `id=${encodeURIComponent(runOpt.id)}&loop=${runOpt.loop?1:0}&epoch=${epoch}`;
  const res = await fetch('/api/sequence/schedule', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  });
  const msg = await res.text();
  if (!res.ok) {
    alert(`No se pudo programar: ${msg || res.status}`); return;
  }

  closeRunOptions();
  updateSequenceStatus();      // refresca barra de estado
  alert(`Secuencia ${runOpt.id + 1} programada correctamente.`);
}

async function cancelSchedule() {
  const res = await fetch('/api/sequence/schedule', { method: 'DELETE' });
  if (res.ok) updateSequenceStatus();
}

function formatRTC(d) {
  const pad = n => String(n).padStart(2, '0');
  const dd = pad(d.getDate());
  const mm = pad(d.getMonth() + 1);
  const yyyy = d.getFullYear();
  const hh = pad(d.getHours());
  const mi = pad(d.getMinutes());
  const ss = pad(d.getSeconds());
  return `${dd}/${mm}/${yyyy} ${hh}:${mi}:${ss}`;
}

async function startRtcClock() {
  const el = document.getElementById('rtcClock');
  if (!el) return;

  const resync = async () => {
    try {
      const r = await fetch('/api/time/now', { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const { epoch } = await r.json();
      rtcDriftMs = (epoch * 1000) - Date.now();     // delta ESP32 vs navegador
    } catch (e) {
      // si falla, mantenemos el drift actual (se verá la hora del navegador)
      console.warn('No se pudo leer el RTC:', e);
    }
  };

  const tick = () => {
    const now = new Date(Date.now() + rtcDriftMs);
    el.textContent = formatRTC(now);
  };

  // primer sync + pintado inmediato
  await resync();
  tick();

  clearInterval(rtcTickTimer);
  rtcTickTimer = setInterval(tick, 1000);     // actualiza cada segundo

  clearInterval(rtcResyncTimer);
  rtcResyncTimer = setInterval(resync, 60000); // corrige drift cada 60 s
}

function openReadingsModal() {
  document.getElementById('readingsModal').style.display = 'block';
  updateReadings();
  readingsTimer = setInterval(updateReadings, 1000);
}

function closeReadingsModal() {
  document.getElementById('readingsModal').style.display = 'none';
  if (readingsTimer) {
    clearInterval(readingsTimer);
    readingsTimer = null;
  }
}

async function updateReadings() {
  try {
    // Nuevo endpoint del ESP32 (ver sección Arduino)
    const r = await fetch('/api/raw_sensors', { cache: 'no-store' });
    const d = await r.json();

    document.getElementById('rawTurbV').textContent = fmt(d.turbidityV, 4);
    document.getElementById('rawPhV').textContent   = fmt(d.phV, 4);
    document.getElementById('convTurb').textContent = fmt(d.turbidityMCmL, 2);
    document.getElementById('convPh').textContent   = fmt(d.phValue, 2);
  } catch (e) {
    console.error('Error leyendo ADS:', e);
    ['rawTurbV','rawPhV','convTurb','convPh'].forEach(id => {
      document.getElementById(id).textContent = 'NaN';
    });
  }
}

function closeModal() {
   document.getElementById('configModal').style.display = 'none';
}

// Actualización periódica
function startDataUpdate() {
    updateSensorData();
    updateLedStatus();
    updateSequenceStatus();
    updateAireacionStatus();
    updateFillingStatus();
    updatePhStatus();
    getTempLimits();
    loadPhLimit();

    setInterval(() => {
        updateSensorData();
        updateLedStatus();
        updateSequenceStatus();
        updateAireacionStatus();
        updateFillingStatus();
        updatePhStatus();
        checkAlarmStatus();
    }, UPDATE_INTERVAL);
}