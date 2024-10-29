# radiomanager-lib-dev
NRF24 Radio Manager Arduino Library

## Project Description

This project focuses on developing a secure radio communication library (currently based on the ESP32 platform) using the Nordic NRF24L01+ module. It features dynamic addressing management, message fragmentation, and encryption. The goal is to enable "on-the-field" pairing of devices with unique UIDs and to easily integrate this into various embedded applications such as gateways and sensors.

### Why NRF24?

The NRF24L01+ is chosen for its cost-effectiveness, reliability, and ease of implementation. It offers integrated functions like ESB and automatic ACK, with other benefits especially related to using the 2.4 GHz band :
- Minimal regulatory constraints allowing transmission up to 100 mW for ranges of several hundred meters to several kilometers with the right antenna
- Channels available just above WiFi frequencies, avoiding interference with most WiFi devices and hotspots
- Unlike LoRa, there is no duty cycle limitation. 
This makes it a cost-effective wireless solution with decent data rates, capable of streaming data at several hundred kb/s. With encryption, it becomes a powerful tool for industrial contexts and embedded applications.
Many "packaged" modules are available at a retail price of 5-10€, such as EBYTE E01-ML01DP5 or E01-2G4M27D with embedde power amplifier.

### Current Capabilities

The RadioManager library currently allows to easily implement communication capabilities with up to 5 other nodes in a point-to-point manner, with fully automatic pairing management and a user-friendly API for sending and receiving messages.

### Encryption Approach

The encryption approach is pragmatic, based on end-to-end encryption using a shared key established during pairing. The aim is to prevent eavesdropping and impersonation (even without knowing the key, an attacker could replay a message to execute an action at will if communicating with an industrial equipment for example). As long as the attacker does not have access to the hardware, communication shoud remain secure and only vulnerable to jamming.

### Future Plans

In the next steps, I plan to enhance the RF24Mesh library to integrate these features (UID-based addressing, dynamic pairing, and encryption) for an even more practical solution (automatic formation and reconfiguration of a mesh network with up to 200+ nodes).

### Further Details

For a detailed understanding of the architecture and specifics, you can explore the code and the README of the RadioManager library. It provides insights into the library's structure and functionality.