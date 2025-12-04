#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h> 

// --- Configurações do INA219 ---
#define I2C_DEVICE "/dev/i2c-1"
#define INA219_ADDR 0x40          
#define SHUNT_RESISTOR_OHM 0.1  

#define INA219_REG_BUS_VOLTAGE 0x02
#define INA219_REG_SHUNT_VOLTAGE 0x01

// IDs dos sensores DS18B20
const char *sids[] = {
    "28-000000000001", 
    "28-000000000002", 
    
};

#define NUM_SENSORS 2


int sensors_init() {
    int fd = open(I2C_DEVICE, O_RDWR);
    if (fd < 0) return -1;
    return fd;
}

void sensors_close(int fd) {
    if (fd >= 0) close(fd);
}


double read_ds18b20(const char *id) {
    char path[128];
    snprintf(path, sizeof(path), "/sys/bus/w1/devices/%s/w1_slave", id);

    FILE *f = fopen(path, "r");
    if (!f) return NAN;

    char line[256];
    
    if (!fgets(line, sizeof(line), f)) { fclose(f); return NAN; }
    if (strstr(line, "YES") == NULL) {
        fclose(f);
        return NAN;
    }

    if (!fgets(line, sizeof(line), f)) { fclose(f); return NAN; }
    fclose(f);

    char *tpos = strstr(line, "t=");
    if (!tpos) return NAN;

    long t = strtol(tpos + 2, NULL, 10);
    return (double)t / 1000.0;
}

// Leitura Tensão INA219
double read_ina219_voltage(int fd, uint8_t addr) {
    if (fd < 0) return NAN;

    if (ioctl(fd, I2C_SLAVE, addr) < 0) return NAN;

    uint8_t reg = INA219_REG_BUS_VOLTAGE;
    if (write(fd, &reg, 1) != 1) return NAN;

    uint8_t buf[2];
    if (read(fd, buf, 2) != 2) return NAN;

    uint16_t raw = (buf[0] << 8) | buf[1];

    return (raw >> 3) * 0.004; 
}

// Leitura Corrente INA219 
double read_ina219_current(int fd, uint8_t addr, double shunt_resistor) {
    if (fd < 0) return NAN;

    if (ioctl(fd, I2C_SLAVE, addr) < 0) return NAN;

    uint8_t reg = INA219_REG_SHUNT_VOLTAGE;
    if (write(fd, &reg, 1) != 1) return NAN;

    uint8_t buf[2];
    if (read(fd, buf, 2) != 2) return NAN;

    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);

  
    double shunt_voltage = raw * 0.00001;


    return shunt_voltage / shunt_resistor; 
}


int main() {

    // 1. Inicializa I2C
    int i2c_fd = sensors_init();

    // 2. Leitura dos DS18B20
    double temps[NUM_SENSORS];
    for (int i = 0; i < NUM_SENSORS; i++) {
        temps[i] = read_ds18b20(sids[i]);
    }

    // 3. Leitura do INA219
    double voltage_v = read_ina219_voltage(i2c_fd, INA219_ADDR);
    double current_a = read_ina219_current(i2c_fd, INA219_ADDR, SHUNT_RESISTOR_OHM);
    
    double current_ma = isnan(current_a) ? NAN : (current_a * 1000.0);

    sensors_close(i2c_fd);

    // JSON
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    char time_str[30];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", t);

    printf("{\n");
    printf("  \"timestamp\": \"%s\",\n", time_str);
    
    printf("  \"ina219\": {\n");
    
    printf("    \"voltage_v\": ");
    if(isnan(voltage_v)) printf("null,\n");
    else printf("%.3f,\n", voltage_v);

    printf("    \"current_ma\": ");
    if(isnan(current_ma)) printf("null\n");
    else printf("%.3f\n", current_ma);
    
    printf("  },\n");

    printf("  \"ds18b20_temperatures_c\": [\n");
    for(int i = 0; i < NUM_SENSORS; i++){
        printf("    {\"id\": \"%s\", \"temp_c\": ", sids[i]);
        
        if(isnan(temps[i])) printf("null}");
        else printf("%.3f}", temps[i]);

        if(i < NUM_SENSORS - 1) printf(",");
        printf("\n");
    }
    printf("  ]\n");
    printf("}\n");

    return 0;
}
