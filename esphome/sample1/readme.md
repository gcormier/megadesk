# Notes
The only other changes I've made from the files checked into  GitHub are to remove buttons 3-4 and rename button 1 to 'sitting' and button 2 to 'standing'

## automations.yaml
This checks to see if it's a work day (not a holiday) and if my iPhone is home and then sets the desk to standing. At the end of the day it sets it to sitting (to avoid skewing any stats)

## Mini graph card
```
type: custom:mini-graph-card
entities:
  - entity: sensor.standing_today
name: Time Spent Standing
hours_to_show: 168
group_by: date
aggregate_func: max
show:
  graph: bar
tap_action:
  action: more-info
  entity: button.standing
color_thresholds:
  - color: '#e45e65'
    value: 0
  - color: '#e0b400'
    value: 1
  - color: '#0da035'
    value: 2
color_thresholds_transition: hard
```