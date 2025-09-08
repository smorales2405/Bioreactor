// Estado de la aplicación
let sensorData = {
    temperature: [],
    ph: [],
    timestamps: []
};

let sequenceRunning = false;
let currentSequence = null;
let sequences = [];

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

// Configuración de actualización
const UPDATE_INTERVAL = 2000; // 2 segundos

// Inicialización
document.addEventListener('DOMContentLoaded', function() {
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
               grid: {
                   color: 'rgba(255, 255, 255, 0.1)'
               },
               ticks: {
                   color: '#94a3b8'
               }
           },
           y: {
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
                   max: 50
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
           datasets: [{
               label: 'pH',
               data: [],
               borderColor: '#10b981',
               backgroundColor: 'rgba(16, 185, 129, 0.1)',
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
                   max: 14
               }
           }
       }
   });
}

// Actualización de datos de sensores
async function updateSensorData() {
   try {
       const response = await fetch(`/api/sensors`);
       const data = await response.json();
       
       // Actualizar valores en pantalla
       document.getElementById('tempValue').textContent = data.temperature.toFixed(1);
       document.getElementById('phValue').textContent = data.ph.toFixed(2);
       document.getElementById('concValue').textContent = data.turbidity.toFixed(1);
       
       // Actualizar gauges
       updateTempGauge(data.temperature);
       updatePhIndicator(data.ph);
       updateConcGauge(data.turbidity);
       
       // Agregar datos a histórico
       const timestamp = new Date().toLocaleTimeString();
       sensorData.timestamps.push(timestamp);
       sensorData.temperature.push(data.temperature);
       sensorData.ph.push(data.ph);
       
       // Limitar datos a últimos 20 puntos
       if (sensorData.timestamps.length > 50) {
           sensorData.timestamps.shift();
           sensorData.temperature.shift();
           sensorData.ph.shift();
       }
       
       // Actualizar gráficos
       updateCharts();
       
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
   tempChart.data.labels = sensorData.timestamps;
   tempChart.data.datasets[0].data = sensorData.temperature;
   tempChart.update();
   
   phChart.data.labels = sensorData.timestamps;
   phChart.data.datasets[0].data = sensorData.ph;
   phChart.update();
}

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
       const response = await fetch(`/status`);
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
                       `<button class="btn btn-primary" onclick="startSequenceModal(${seq.id})">Ejecutar</button>` : 
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
       
       if (data.running) {
           sequenceRunning = true;
           statusText.innerHTML = `Ejecutando Secuencia ${data.sequenceId + 1} - Paso ${data.currentStep + 1}/${data.totalSteps} 
                                  (${data.elapsedHours}h ${data.elapsedMinutes}m ${data.elapsedSeconds}s / 
                                   ${data.totalHours}h ${data.totalMinutes}m ${data.totalSeconds}s)`;
           stopBtn.style.display = 'inline-block';
       } else {
           sequenceRunning = false;
           statusText.textContent = 'Inactivo';
           stopBtn.style.display = 'none';
       }
   } catch (error) {
       console.error('Error obteniendo estado de secuencia:', error);
   }
}

function startSequenceModal(id) {
   const modal = document.createElement('div');
   modal.className = 'modal';
   modal.style.display = 'block';
   modal.innerHTML = `
       <div class="modal-content">
           <span class="close" onclick="this.parentElement.parentElement.remove()">&times;</span>
           <h2>Ejecutar Secuencia ${id + 1}</h2>
           <div class="execution-mode">
               <label>
                   <input type="radio" name="mode" value="once" checked>
                   Ejecutar una vez
               </label>
               <label>
                   <input type="radio" name="mode" value="loop">
                   Ejecutar en bucle
               </label>
           </div>
           <div class="modal-buttons">
               <button class="btn btn-primary" onclick="executeSequence(${id}, document.querySelector('input[name=mode]:checked').value); this.parentElement.parentElement.parentElement.remove()">Iniciar</button>
               <button class="btn btn-secondary" onclick="this.parentElement.parentElement.parentElement.remove()">Cancelar</button>
           </div>
       </div>
   `;
   document.body.appendChild(modal);
}

async function executeSequence(id, mode) {
   try {
       // Implementar modo bucle si es necesario
       await fetch(`/api/sequence/${id}/start`, { method: 'POST' });
       loadSequences();
   } catch (error) {
       console.error('Error ejecutando secuencia:', error);
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

function configureSequence(id) {
   currentSequence = id;
   document.getElementById('seqNumber').textContent = id + 1;
   document.getElementById('configModal').style.display = 'block';
   updateStepsConfig();
}

function updateStepsConfig() {
   const stepCount = parseInt(document.getElementById('stepCount').value);
   const container = document.getElementById('stepsContainer');
   container.innerHTML = '';
   
   for (let i = 0; i < stepCount; i++) {
       const stepDiv = document.createElement('div');
       stepDiv.className = 'step-config';
       stepDiv.innerHTML = `
           <h4>Paso ${i + 1}</h4>
           <div class="color-controls">
               <div class="color-control">
                   <label>Blanco:</label>
                   <input type="range" id="step${i}_white" min="0" max="10" value="0">
                   <span>0</span>
               </div>
               <div class="color-control">
                   <label>Rojo:</label>
                   <input type="range" id="step${i}_red" min="0" max="10" value="0">
                   <span>0</span>
               </div>
               <div class="color-control">
                   <label>Verde:</label>
                   <input type="range" id="step${i}_green" min="0" max="10" value="0">
                   <span>0</span>
               </div>
               <div class="color-control">
                   <label>Azul:</label>
                   <input type="range" id="step${i}_blue" min="0" max="10" value="0">
                   <span>0</span>
               </div>
           </div>
           <div class="time-controls">
               <div class="time-control">
                   <label>Horas:</label>
                   <input type="number" id="step${i}_hours" min="0" max="23" value="0">
               </div>
               <div class="time-control">
                   <label>Minutos:</label>
                   <input type="number" id="step${i}_minutes" min="0" max="59" value="0">
               </div>
               <div class="time-control">
                   <label>Segundos:</label>
                   <input type="number" id="step${i}_seconds" min="0" max="59" value="0">
               </div>
           </div>
       `;
       container.appendChild(stepDiv);
       
       // Agregar listeners para actualizar valores
       stepDiv.querySelectorAll('input[type="range"]').forEach(input => {
           input.addEventListener('input', function() {
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
           await fetch(`/api/sequence/${id}/delete`, { method: 'DELETE' });
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
            } else {
                fillingStatusText.textContent = 'Bomba Manual Activa';
                fillingProgress.style.display = 'none';
                document.getElementById('startFillBtn').style.display = 'inline-block';
                document.getElementById('stopFillBtn').style.display = 'none';
            }
        } else {
            fillingActive = false;
            pumpIndicator.classList.remove('active');
            pumpStatus.textContent = 'Bomba APAGADA';
            fillingStatusText.textContent = 'Inactivo';
            fillingProgress.style.display = 'none';
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
    }
}

// Checkbox de modo bucle
document.addEventListener('DOMContentLoaded', function() {
    const loopCheckbox = document.getElementById('co2LoopMode');
    if (loopCheckbox) {
        loopCheckbox.addEventListener('change', function() {
            document.getElementById('loopConfig').style.display = 
                this.checked ? 'block' : 'none';
            co2LoopMode = this.checked;
        });
    }
});

// Iniciar inyección de CO2
async function startCO2Injection() {
    if (co2InjectionActive) {
        alert('Ya hay una inyección en progreso');
        return;
    }
    
    const minutes = parseInt(document.getElementById('co2Minutes').value) || 0;
    
    if (minutes === 0) {
        alert('Por favor configure el tiempo de inyección');
        return;
    }
    
    co2InjectionTime = minutes * 60; // Convertir a segundos
    co2TimeRemaining = co2InjectionTime;
    co2InjectionActive = true;
    
    if (co2LoopMode) {
        co2LoopInterval = parseInt(document.getElementById('loopInterval').value) || 4;
    }
    
    try {
         await fetch(`/api/co2/manual/start`, {
             method: 'POST',
             body: JSON.stringify({ 
                 minutes: minutes,
                 loop: co2LoopMode,
                 interval: co2LoopInterval
             })
         });
        
        startCO2Timer();
        console.log(`Inyección de CO2 iniciada: ${minutes} minutos`);
    } catch (error) {
        console.error('Error iniciando inyección:', error);
    }
}

// Timer de CO2
function startCO2Timer() {
    if (co2Timer) clearInterval(co2Timer);
    
    co2Timer = setInterval(() => {
        if (co2TimeRemaining > 0) {
            co2TimeRemaining--;
            updateCO2TimerDisplay();
        } else {
            // Tiempo expirado
            document.getElementById('timerDisplay').classList.add('expired');
            
            if (co2LoopMode) {
                // Reiniciar en modo bucle
                setTimeout(() => {
                    co2TimeRemaining = co2InjectionTime;
                    document.getElementById('timerDisplay').classList.remove('expired');
                }, co2LoopInterval * 3600000); // Convertir horas a ms
            } else {
                // Detener
                co2InjectionActive = false;
                clearInterval(co2Timer);
            }
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
async function pauseCO2Injection() {
    if (!co2InjectionActive) return;
    
    try {
        await fetch(`/api/co2/manual/pause`, { method: 'POST' });
        
        if (co2Timer) clearInterval(co2Timer);
        console.log('Inyección pausada');
    } catch (error) {
        console.error('Error pausando inyección:', error);
    }
}

// Reiniciar inyección
async function resetCO2Injection() {
    try {
        await fetch(`/api/co2/manual/reset`, { method: 'POST' });
        
        co2InjectionActive = false;
        co2TimeRemaining = 0;
        if (co2Timer) clearInterval(co2Timer);
        
        document.getElementById('co2Minutes').value = 0;
        document.getElementById('co2TimeRemaining').textContent = '00:00';
        document.getElementById('timerFill').style.width = '0%';
        document.getElementById('timerDisplay').classList.remove('expired');
        
        console.log('Inyección reiniciada');
    } catch (error) {
        console.error('Error reiniciando inyección:', error);
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
            const response = await fetch(`${SERVER_URL}/api/ph/status`);
            const data = await response.json();
            
            document.getElementById('phCurrentLarge').textContent = data.phValue.toFixed(1);
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
        } catch (error) {
            console.error('Error actualizando estado pH:', error);
        }
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

    setInterval(() => {
        updateSensorData();
        updateLedStatus();
        updateSequenceStatus();
        updateAireacionStatus();
        updateFillingStatus();
        updatePhStatus();
    }, UPDATE_INTERVAL);
}