# Smart Pet Feeder & Water Dispenser (ESP32) & Mobile App

An automated pet feeding and watering system powered by the **ESP32 microcontroller**.  
The system takes care of your pet’s food and water needs without constant owner interaction.  

Once the system is initialized with a few inputs (feeding schedule, portion size, and water thresholds), it automatically:  
- [x] Dispenses food at scheduled times using a servo motor  
- [x] Measures food weight with an **HX711 load cell** to ensure correct portions  
- [x] Refills water using a pump based on readings from the **water level sensor**  
- [x] Detects pet presence with an **IR sensor**  
- [x] Alerts with a **buzzer** when needed (e.g., low food/water, system error)  
- [x] Operates independently after setup, reducing manual effort  

---

## Features
- Automated food dispensing at configurable times  
- Precise food portion control with load cell integration  
- Smart water refilling based on real-time level detection  
- Pet presence detection for smarter operation  
- Audible alerts using buzzer  
- ESP32-based, easily programmable and Wi-Fi capable  

---

## Hardware Used
- ESP32 (main controller)  
- Servo Motor (food dispensing mechanism)  
- Water Pump (automatic water refilling)  
- IR Sensor (pet presence detection)  
- Water Level Sensor (monitoring bowl levels)  
- HX711 + Load Cell (food weight measurement)  
- Buzzer (alert system)  

---

## Goal
This project provides pet owners with **convenience, reliability, and peace of mind** — ensuring their pets are always fed and hydrated, even when they are away or busy.  

---

## Collaborators
- **[@tAwFiK2005](https://github.com/tAwFiK2005)** – Project Lead, Documentation and repository maintenance  
- **[@Ahmed-Ayman-2825](https://github.com/Ahmed-Ayman-2825)** – Hardware setup, wiring, and testing  
- **[@ashrafeesa](https://github.com/ashrafeesa)** – Flutter application 
- **[@ahmedabdalwahab](https://github.com/ahmedabdalwahab)** – ESP32 programming, system integration, Sensor calibration, load cell & water level system
- - **[@hazemsaeedkamel](https://github.com/hazemsaeedkamel)** – Data Flow & Data Integration

---
