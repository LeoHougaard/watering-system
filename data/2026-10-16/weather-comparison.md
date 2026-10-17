# Weather comparison for the moisture probe

The longer moisture trend is broadly consistent with North Vancouver's modeled drying and rain. The abrupt October 13 to 15 collapse is not consistent with ordinary drying alone. It happened during a wet period, repeatedly reached the ADC's maximum value, and ended with a rapid return to high readings on a day with no modeled rain. A sensor or electrical signal problem remains plausible. Weather cannot identify the failed component or confirm the soil's actual moisture.

## Longer trend

Compared the controller's unaggregated hourly readings with Open-Meteo archive estimates at the project's existing approximate North Vancouver coordinates, 49.32, -123.07. Daily sensor means use PDT.

| Period | Modeled rain | Reference evapotranspiration | Probe daily means | Interpretation |
| --- | ---: | ---: | ---: | --- |
| September 6 to 22 | 9.9 mm | 40.27 mm | 80.67% to 41.33% | A gradual drying trend is plausible. Watering ran on September 20 to 22. |
| September 23 to October 12 | 115.4 mm | 31.17 mm | 53.29% to 89.79% | A return to high readings is plausible if rain reaches the probe's container. No scheduled watering was logged in this period. |
| October 13 to 14 | 15.2 mm | 2.07 mm | 86.88% to 7.50% | The abrupt drop during modeled rain is inconsistent with the earlier trend. |

Reference evapotranspiration is a comparison of drying conditions, not measured water loss from these pots. Neither rainfall minus reference evapotranspiration nor a probe percentage is a direct container water balance.

## October 13 to 15 timeline

- October 13, 21:58 PDT: 91%, raw 1361. At 22:58 it was 78%, raw 1590, and by 23:58 it was 15%, raw 2727.
- The archive model places rain in the evening and overnight, about 19:00 October 13 through 07:00 October 14. Hourly rain values describe the preceding hour, so the bar labeled midnight includes rain before midnight. The modeled overnight temperatures were about 10 to 11 C and relative humidity rose toward 90%.
- October 14, 01:58: the probe reported 0%, raw 3120. This was before the 06:00 watering check. It later reached raw 4095 repeatedly.
- October 14 and 15: each day's scheduled watering comprised a two-minute pulse at about 06:00 and the remaining eight minutes at about 06:32 after insufficient probe response. Both days stayed within the ten-minute allowance.
- October 15, 12:58: 0%, raw 4095. One hour later it was 89%, raw 1397, and at 14:58 it was 100%, raw 1183. Both model datasets and the YVR daily record show no rain that day. Earlier rain or morning irrigation could have reached the probe after a delay; the timing alone does not prove electrical recovery.

Seventeen of the stored five-reading averages reached raw 4095. This was sustained across the averaging windows, not merely a graph-label error. The existing 3000-dry/1200-wet calibration converts readings at or above the dry endpoint to 0%. Replaying that exact arithmetic for all 984 recent records produced zero mismatches. The saturation is already present in the recorded raw signal.

## Independent weather checks

Environment and Climate Change Canada's North Vancouver Wharves gauge, station 833 and climate ID 1105669, reports 10.0 mm on October 13 and 0.0 mm on October 14. October 15 and 16 precipitation is missing in the retrieved response. The gauge is about 3.3 km west of the selected point and its reporting day is not assumed to match the model's midnight-to-midnight hourly windows.

The more distant Vancouver International Airport station, 51442 and climate ID 1108395, reports 1.2 mm on October 13, 3.3 mm on October 14, and 0.0 mm on October 15. Its daily highs on those days were 14.7, 15.2, and 14.9 C. This is a regional cross-check, not a measurement of rainfall at the pots.

Open-Meteo's archive estimates 6.0 and 9.2 mm on October 13 and 14. Its separate historical forecast series estimates 4.3 and 13.7 mm. Both show zero rain on October 15. These products agree on a wet spell but differ in exact totals. They are model estimates, not independent rain gauges. Exact source queries, response data, units and hashes are in [weather-sources.json](weather-sources.json).

## What can be concluded

Normal autumn rain provides a reasonable explanation for the high readings before and after the event if the probe's pot is exposed to that rain. It does not explain the sudden apparent drying during the wet spell. The fall began before either of the affected morning pump runs, so those runs cannot explain its initial onset.

GPIO1 is configured as ADC1 channel 0 at 12 dB attenuation. The inspected controller code contains no other ADC user or GPIO1 output. Probe GPIO, dry/wet calibration, averaging count and live sample interval match the September 5 archive. Retained diagnostics contain no valid-time OTA entry after September 6. These observations do not rule out an intermittent ADC, supply, wiring or sensor fault, and the firmware does not retain enough electrical diagnostics to distinguish them.

No software cause was reproduced, so no firmware, calibration, or watering behavior was changed. Leo cannot inspect the soil now. To confirm the cause later, compare the displayed reading with the soil at the probe and inspect its analog, ground and supply connections. If the problem recurs, measure the sensor output and supply while recording the displayed raw value. The actual water delivery and rain exposure of this pot remain unknown.

Sources: [Open-Meteo archive documentation](https://open-meteo.com/en/docs/historical-weather-api), [historical forecast documentation](https://open-meteo.com/en/docs/historical-forecast-api), [ECCC North Vancouver Wharves daily records](https://api.weather.gc.ca/collections/climate-daily/items?CLIMATE_IDENTIFIER=1105669&datetime=2026-10-10T00:00:00Z/2026-10-16T23:59:59Z&limit=20&f=json), and [Espressif ADC documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/adc_oneshot.html). Weather comparisons and all 42 daily rows are in [weather-comparison.json](weather-comparison.json).
