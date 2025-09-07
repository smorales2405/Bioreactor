// Configuración del servidor
const ESP32_IP = '192.168.4.1'; // IP del ESP32 en modo AP
const SERVER_URL = `http://${ESP32_IP}`;

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

// Configuración de actualización
const UPDATE_INTERVAL = 2000; // 2 segundos

// Inicialización
document.addEventListener('DOMContentLoaded', function() {
    initCharts();
    loadSequences();
    startDataUpdate();
    updateConnectionStatus(true);
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
       const response = await fetch('${SERVER_URL}/api/sensors');
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
       if (sensorData.timestamps.length > 20) {
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
       await fetch(`${SERVER_URL}/led/${colorMap[color]}/pwm/${value}`);
       
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
       const response = await fetch('${SERVER_URL}/status');
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
       const response = await fetch('${SERVER_URL}/api/sequences');
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
       const response = await fetch('${SERVER_URL}/api/sequence/status');
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
       await fetch(`${SERVER_URL}/api/sequence/${id}/start`, { method: 'POST' });
       loadSequences();
   } catch (error) {
       console.error('Error ejecutando secuencia:', error);
   }
}

async function stopSequence() {
   try {
       await fetch('${SERVER_URL}/api/sequence/stop', { method: 'POST' });
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
       await fetch('${SERVER_URL}/api/sequence/save', {
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
           await fetch(`${SERVER_URL}/api/sequence/${id}/delete`, { method: 'DELETE' });
           loadSequences();
       } catch (error) {
           console.error('Error borrando secuencia:', error);
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
   
   setInterval(() => {
       updateSensorData();
       updateLedStatus();
       updateSequenceStatus();
   }, UPDATE_INTERVAL);
}