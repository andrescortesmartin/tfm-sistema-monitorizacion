var dp = msg.values || msg;
return dp.ts_ambiente !== undefined || 
       dp.ts_angulo !== undefined || 
       dp.ts_gps !== undefined ||
       dp.ts_ranging !== undefined ||
       dp.ts_beacons !== undefined;