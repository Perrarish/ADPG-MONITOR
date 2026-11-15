const ESP32_IP = "10.61.177.10";
const ESP32_URL = `http://${ESP32_IP}/api/data`;
const HISTORY_LIMIT = 60;

document.getElementById("espIp").textContent = ESP32_IP;

const fields = [
    "time_ms",
    "rpm",
    "generator_voltage",
    "generator_current",
    "generator_power",
    "battery_temp",
    "bottom_temp",
    "top_temp",
    "temperature_difference",
    "battery_voltage",
    "battery_current",
    "battery_power",
    "battery_status",
    "phone_voltage",
    "phone_current",
    "phone_power",
    "pressure",
    "pressure_temperature"
];

const csvColumns = [
    ["Timestamp", "timestamp"],
    ["Operating Mode", "mode"],
    ["RPM", "rpm"],
    ["Generator Voltage", "generator_voltage"],
    ["Generator Current", "generator_current"],
    ["Generator Power", "generator_power"],
    ["Battery Voltage", "battery_voltage"],
    ["Battery Current", "battery_current"],
    ["Battery Power", "battery_power"],
    ["Battery Status", "battery_status"],
    ["Phone Voltage", "phone_voltage"],
    ["Phone Current", "phone_current"],
    ["Phone Power", "phone_power"],
    ["Battery Temperature", "battery_temp"],
    ["Bottom Temperature", "bottom_temp"],
    ["Top Temperature", "top_temp"],
    ["Temperature Difference", "temperature_difference"],
    ["Pressure", "pressure"],
    ["Pressure Temperature", "pressure_temperature"]
];

const histories = {
    live: [],
    demo: []
};
const sessionReadings = [];
const charts = {};
const demoStartedAt = Date.now();
let currentMode = "live";
let latestLiveData = null;
let esp32Connected = false;
let demoState = null;
let demoPhoneConnected = true;
let nextPhoneConnectionChangeAt = 0;

function displayValue(value) {
    if (value === null || value === undefined || value === "") {
        return "--";
    }

    if (typeof value === "number") {
        return Number.isInteger(value) ? value : value.toFixed(2);
    }

    return value;
}

function numericValue(value) {
    if (value === null || value === undefined || value === "") {
        return null;
    }

    const number = typeof value === "number" ? value : Number(value);
    return Number.isFinite(number) ? number : null;
}

function updateDashboard(data) {
    fields.forEach(field => {
        const element = document.getElementById(field);
        if (element) {
            element.textContent = displayValue(
                Object.prototype.hasOwnProperty.call(data, field) ? data[field] : null
            );
        }
    });
}

function markReadingsUnavailable() {
    fields.forEach(field => {
        const element = document.getElementById(field);
        if (element) {
            element.textContent = "--";
        }
    });
}

function setConnectionStatus(connected, message = "") {
    const dot = document.getElementById("statusDot");
    const status = document.getElementById("connectionStatus");
    const indicator = document.getElementById("connectionIndicator");

    esp32Connected = connected;
    dot.classList.toggle("online", connected);
    dot.classList.toggle("offline", !connected);
    indicator.classList.toggle("is-connected", connected);
    indicator.classList.toggle("is-disconnected", !connected);
    status.textContent = connected ? "ESP32 Connected" : "ESP32 Disconnected";

    if (!connected) {
        document.getElementById("errorMessage").textContent =
            message || "Unable to receive data from ESP32";
        if (currentMode === "live") {
            markReadingsUnavailable();
        }
    } else if (currentMode === "demo") {
        document.getElementById("errorMessage").textContent =
            "Demo mode active — simulated readings only";
    }
}

function recordReading(data, mode) {
    const reading = {
        timestamp: new Date().toISOString(),
        mode
    };

    fields.forEach(field => {
        if (field in data) {
            reading[field] = data[field];
        }
    });

    const history = histories[mode];
    history.push(reading);
    if (history.length > HISTORY_LIMIT) {
        history.shift();
    }

    sessionReadings.push(reading);
    updateCharts();
}

function updateCharts() {
    if (!window.Chart) {
        return;
    }

    const history = histories[currentMode];
    const labels = history.map(reading =>
        new Date(reading.timestamp).toLocaleTimeString()
    );

    Object.keys(charts).forEach(chartName => {
        const chart = charts[chartName];
        chart.data.labels = labels;
        chart.data.datasets.forEach(dataset => {
            dataset.data = history.map(reading => {
                const value = numericValue(reading[dataset.field]);
                return value === null ? null : value * (dataset.unitScale || 1);
            });
        });
        chart.update("none");
    });
}

function createChart(canvasId, yAxisTitle, datasets) {
    const canvas = document.getElementById(canvasId);
    if (!canvas || !window.Chart) {
        return null;
    }

    return new Chart(canvas, {
        type: "line",
        data: {
            labels: [],
            datasets: datasets.map(dataset => ({
                ...dataset,
                data: [],
                fill: false,
                borderWidth: 2,
                pointRadius: 2,
                pointHoverRadius: 5,
                tension: 0.25
            }))
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            interaction: {
                intersect: false,
                mode: "index"
            },
            plugins: {
                legend: {
                    display: true,
                    labels: {
                        color: "#29475a",
                        usePointStyle: true,
                        boxWidth: 8
                    }
                },
                tooltip: {
                    backgroundColor: "#102b3f",
                    titleColor: "#ffffff",
                    bodyColor: "#e5f0f4",
                    borderColor: "#a9c5d1",
                    borderWidth: 1,
                    callbacks: {
                        label(context) {
                            return `${context.dataset.label}: ${context.formattedValue} ${yAxisTitle}`;
                        }
                    }
                }
            },
            scales: {
                x: {
                    title: {
                        display: true,
                        text: "Time",
                        color: "#647b8d"
                    },
                    ticks: {
                        color: "#647b8d",
                        maxTicksLimit: 8,
                        maxRotation: 0
                    },
                    grid: {
                        color: "rgba(80, 111, 130, 0.14)"
                    }
                },
                y: {
                    title: {
                        display: true,
                        text: yAxisTitle,
                        color: "#647b8d"
                    },
                    ticks: {
                        color: "#647b8d"
                    },
                    grid: {
                        color: "rgba(80, 111, 130, 0.14)"
                    }
                }
            }
        }
    });
}

function initializeCharts() {
    if (!window.Chart) {
        console.error("Chart.js did not load; live monitoring remains available.");
        return;
    }

    charts.rpm = createChart("rpmChart", "RPM", [
        { label: "Turbine RPM", field: "rpm", borderColor: "#1687ae" }
    ]);
    charts.batteryVoltage = createChart("batteryVoltageChart", "Volts (V)", [
        { label: "Battery Voltage", field: "battery_voltage", borderColor: "#168a4a" }
    ]);
    charts.batteryCurrent = createChart("batteryCurrentChart", "Milliamps (mA)", [
        { label: "Battery Current", field: "battery_current", borderColor: "#7855b4" }
    ]);
    charts.phoneCurrent = createChart("phoneCurrentChart", "Milliamps (mA)", [
        { label: "Phone Charging Current", field: "phone_current", borderColor: "#b57600" }
    ]);
    charts.phonePower = createChart("phonePowerChart", "Watts (W)", [
        { label: "Phone Charging Power", field: "phone_power", unitScale: 0.001, borderColor: "#d45b8b" }
    ]);
    charts.temperature = createChart("temperatureChart", "Temperature (°C)", [
        { label: "Battery Temperature", field: "battery_temp", borderColor: "#1687ae" },
        { label: "Tower Top Temperature", field: "top_temp", borderColor: "#c54c63" },
        { label: "Tower Bottom Temperature", field: "bottom_temp", borderColor: "#b47b04" }
    ]);
}

function randomBetween(min, max, decimals = 2) {
    const value = min + Math.random() * (max - min);
    return Number(value.toFixed(decimals));
}

function driftValue(previous, min, max, maxStep, decimals = 2) {
    if (!Number.isFinite(previous)) {
        return randomBetween(min, max, decimals);
    }

    const next = Math.max(min, Math.min(max, previous + randomBetween(-maxStep, maxStep, decimals + 1)));
    return Number(next.toFixed(decimals));
}

function generateDemoData() {
    const now = Date.now();
    if (nextPhoneConnectionChangeAt === 0) {
        nextPhoneConnectionChangeAt = now + randomBetween(20000, 40000, 0);
    } else if (now >= nextPhoneConnectionChangeAt) {
        demoPhoneConnected = !demoPhoneConnected;
        nextPhoneConnectionChangeAt = now + randomBetween(20000, 40000, 0);
    }

    const rpm = driftValue(demoState && demoState.rpm, 1800, 2100, 24, 0);
    const generatorVoltage = driftValue(demoState && demoState.generator_voltage, 10, 12, 0.18);
    const maxGeneratorCurrent = Math.min(120, Math.floor(1400 / generatorVoltage));
    const generatorCurrent = driftValue(
        demoState && demoState.generator_current,
        30,
        maxGeneratorCurrent,
        8,
        0
    );
    const generatorPowerWatts = generatorVoltage * (generatorCurrent / 1000);
    const batteryVoltage = driftValue(demoState && demoState.battery_voltage, 13, 13.4, 0.025);
    const batteryCurrent = demoPhoneConnected
        ? driftValue(demoState && demoState.battery_current, 200, 450, 28, 0)
        : driftValue(demoState && demoState.battery_current, -60, -10, 6, 0);
    const phoneVoltage = demoPhoneConnected
        ? driftValue(demoState && demoState.phone_voltage, 4.95, 5.05, 0.008)
        : 0;
    const minPhoneCurrent = demoPhoneConnected
        ? Math.max(500, Math.ceil(2500 / phoneVoltage))
        : 0;
    const maxPhoneCurrent = demoPhoneConnected
        ? Math.min(1000, Math.floor(5000 / phoneVoltage))
        : 0;
    const phoneCurrent = demoPhoneConnected
        ? driftValue(
            demoState && demoState.phone_current,
            minPhoneCurrent,
            maxPhoneCurrent,
            55,
            0
        )
        : 0;
    const phonePowerWatts = phoneVoltage * (phoneCurrent / 1000);
    const batteryTemp = driftValue(demoState && demoState.battery_temp, 33, 37, 0.25);
    const topTemp = driftValue(demoState && demoState.top_temp, 8, 14, 0.35);
    const minDifference = Math.max(6, 16 - topTemp);
    const maxDifference = Math.min(10, 22 - topTemp);
    const temperatureDifference = driftValue(
        demoState && demoState.temperature_difference,
        minDifference,
        maxDifference,
        0.3
    );
    const bottomTemp = Number((topTemp + temperatureDifference).toFixed(2));
    const pressure = driftValue(demoState && demoState.pressure, 0.5, 2, 0.12);
    const pressureTemperature = driftValue(
        demoState && demoState.pressure_temperature,
        20,
        30,
        0.3
    );

    demoState = {
        time_ms: now - demoStartedAt,
        rpm,
        generator_voltage: generatorVoltage,
        generator_current: generatorCurrent,
        generator_power: Number((generatorPowerWatts * 1000).toFixed(0)),
        battery_temp: batteryTemp,
        bottom_temp: bottomTemp,
        top_temp: topTemp,
        temperature_difference: Number(Math.abs(topTemp - bottomTemp).toFixed(2)),
        battery_voltage: batteryVoltage,
        battery_current: batteryCurrent,
        battery_power: Number((batteryVoltage * batteryCurrent).toFixed(0)),
        battery_status: batteryCurrent > 0 ? "Discharging" : "Charging",
        phone_voltage: phoneVoltage,
        phone_current: phoneCurrent,
        phone_power: Number((phonePowerWatts * 1000).toFixed(0)),
        pressure,
        pressure_temperature: pressureTemperature
    };

    return { ...demoState };
}

function setMode(mode) {
    if (mode !== "live" && mode !== "demo") {
        return;
    }

    currentMode = mode;
    const isDemo = mode === "demo";
    document.getElementById("viewTitle").textContent =
        isDemo ? "Demo Monitoring" : "Live Monitoring";
    document.getElementById("modeDescription").textContent = isDemo
        ? "Simulated sensor readings · No simulated data is sent to ESP32"
        : "Real-time sensor data from ESP32";
    document.getElementById("modeIndicator").hidden = !isDemo;
    document.getElementById("chartModeLabel").textContent =
        isDemo ? "Demo" : "Live";

    document.querySelectorAll(".mode-button[data-mode]").forEach(control => {
        const selected = control.dataset.mode === mode;
        control.classList.toggle("is-active", selected);
        control.setAttribute("aria-pressed", String(selected));
    });
    setActiveNavigation("monitoring");

    if (isDemo) {
        const demoData = generateDemoData();
        updateDashboard(demoData);
        recordReading(demoData, "demo");
        document.getElementById("errorMessage").textContent =
            "Demo mode active — simulated readings only";
    } else {
        if (esp32Connected && latestLiveData) {
            updateDashboard(latestLiveData);
            document.getElementById("errorMessage").textContent =
                "Live data received successfully";
        } else {
            markReadingsUnavailable();
            document.getElementById("errorMessage").textContent =
                "Waiting for ESP32 connection...";
        }
        updateCharts();
    }
}

function setActiveNavigation(section) {
    document.querySelectorAll(".main-nav a").forEach(link => {
        const active = link.dataset.section === section &&
            (section !== "monitoring" || link.dataset.mode === currentMode);
        if (active) {
            link.setAttribute("aria-current", "page");
        } else {
            link.removeAttribute("aria-current");
        }
    });
}

function csvEscape(value) {
    const text = value === null || value === undefined ? "" : String(value);
    return `"${text.replace(/"/g, '""')}"`;
}

function downloadCsv() {
    const readings = sessionReadings.filter(reading => reading.mode === currentMode);
    if (readings.length === 0) {
        document.getElementById("errorMessage").textContent =
            `No ${currentMode} readings collected yet; CSV export is unavailable.`;
        return;
    }

    const headers = csvColumns.map(column => column[0]);
    const rows = readings.map(reading =>
        csvColumns.map(([, key]) => key === "timestamp"
            ? reading.timestamp
            : key === "mode"
                ? (reading.mode === "demo" ? "Demo" : "Live")
                : reading[key])
    );
    const csv = [headers, ...rows]
        .map(row => row.map(csvEscape).join(","))
        .join("\r\n");
    const blob = new Blob(["\uFEFF", csv], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = `adpg-readings-${new Date().toISOString().replace(/[:.]/g, "-")}.csv`;
    document.body.appendChild(link);
    link.click();
    link.remove();
    URL.revokeObjectURL(url);
}

async function fetchESP32Data() {
    try {
        const response = await fetch(ESP32_URL, {
            method: "GET",
            cache: "no-store"
        });

        if (!response.ok) {
            throw new Error(`HTTP error: ${response.status}`);
        }

        const data = await response.json();
        if (!data || typeof data !== "object" || Array.isArray(data)) {
            throw new Error("Invalid JSON response from ESP32");
        }

        latestLiveData = data;
        setConnectionStatus(true);
        document.getElementById("lastUpdated").textContent =
            "Last successful update: " + new Date().toLocaleString();

        if (currentMode === "live") {
            updateDashboard(data);
            recordReading(data, "live");
            document.getElementById("errorMessage").textContent =
                "Live data received successfully";
        }
    } catch (error) {
        setConnectionStatus(false, error.message);
        console.error("ESP32 connection error:", error);
    }
}

initializeCharts();
document.querySelectorAll(".mode-button[data-mode]").forEach(control => {
    control.addEventListener("click", event => {
        setMode(control.dataset.mode);
    });
});
document.querySelectorAll(".main-nav a").forEach(link => {
    link.addEventListener("click", () => {
        if (link.dataset.mode) {
            setMode(link.dataset.mode);
        } else {
            setActiveNavigation(link.dataset.section);
        }
    });
});
document.getElementById("downloadCsv").addEventListener("click", downloadCsv);

fetchESP32Data();
setInterval(fetchESP32Data, 2000);
setInterval(() => {
    if (currentMode === "demo") {
        const demoData = generateDemoData();
        updateDashboard(demoData);
        recordReading(demoData, "demo");
    }
}, 2000);
