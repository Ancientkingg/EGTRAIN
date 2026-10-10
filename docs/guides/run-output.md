# Run output

A run writes text files to its output folder ([Output folder](command-line.md#output-folder)), and the paths below are relative to that folder.

| File | Written | Content |
| --- | --- | --- |
| `EnergyConsumptionPerTrain.txt` | At the end of the run. Replaces an existing file | A header line, then one row per train: `TrainID`, `TotEnergyConsumed[KWh]`, `TotEnergyConsumedWithRegen[KWh]`, `TotEnergyRequestAtSubst[KWh]` and `TotEnergyRequestAtSubstWithRegen[KWh]`, separated by spaces |
| `TotalEnergyConsumption.txt` | At the end of the run. Replaces an existing file | A header line and one row with the sums over all trains of the same four quantities, separated by spaces |
| `TrainTrajectories/Stats_Stations.txt` | At the end of the run. Replaces an existing file | Delay statistics per station; the columns are in [How delays are calculated](delay-analysis.md#the-station-statistics-files) |
| `TrainTrajectories/Pos&Neg_Stats_Stations.txt` | At the end of the run. Replaces an existing file | The same statistics with the signed delay; see [How delays are calculated](delay-analysis.md#the-station-statistics-files) |
| `TrainTrajectories/TimetablePoints.txt` | At the end of the run. Replaces an existing file | No header line. Six lines per train: the train description and type, then one line each with the ids, the positions (written twice), the first times and the second times of its timetable points, separated by spaces |
| `TrainTrajectories/BlockingTimes.txt` | At the end of the run. Replaces an existing file | No header line. Six lines per train: the train description, then one line each with the block ids, `GeoPosStart`, `GeoPosEnd`, `StartOccTime` and `EndOccTime` of its blocking times, separated by spaces |
| `TrainTrajectories/TrainServicePathDiagram.txt` | After the simulation returns, not during it: `DispatchController::printLastTrainServicePathDiagram` writes it. The application calls that function after a run without a window and after a run in the window that was not stopped. Appends one row per train | No header line. One row per train whose departure time is inside the simulated time, separated by tabs: the train description, the line id, the reversed direction flag, the corridor, then one cell per time step with the position of the train, empty where the train has no position. A second run into the same folder adds its rows after the first |
| `TrainTrajectories/Computing_Times.txt` | At the end of the run. Replaces an existing file | A header line with `TOT_Comp_Time_EGTRAIN[s]`, `TOT_Comp_Time_ROMA` and `TOT_COMP_TIME`, and one row with the three values |
| `PassengerStatus/PassengerStatus.txt` | At every time step. Each write replaces the file, so a finished run holds the last step | A header line that names the columns, then one row per passenger who is in the network at that step |
| `PassengerStatus/JourneyDelays.txt` | At the end of the run. Replaces an existing file | A header line with `PaxID`, `JourneyID` and `TotalArrivalDelay[s]`, then one row per journey of every passenger; the delay is `Journey_not_yet_completed` for a journey that is not completed |
| `Rescheduling/EGTRAINOutput.txt` | When a train stops at its last station. Appends one row | No header line. Each row has the values `egtrain`, the station name, `arr`, the line id, the index of the train and the time step, separated by commas. A second run into the same folder adds its rows after the first |

`TrainTrajectories/RoutesGenerated` is created empty. Nothing writes into it.

## Detailed trajectories

Two more files are written only when the application is started with `--detailed-trajectories`:

- `TEMP/Traj_Train_<train description>.txt`, one file for each train. The simulation writes them as its last stage and creates the folder `TEMP` for them.
- `TrainTrajectories/TrainPathDiagram.txt`, one file with a row for every train and a column for every time step.

Both replace an existing file of the same name.

```bash
QEGTRAIN --scene path/to/scene -g 0 --detailed-trajectories
```

The first line of a train file names eight columns, separated by tabs: `Time[s]`, `Speed[m/s]`, `Position[m]`, `Tail_Position[m]`, `Power_Cons[kW]`, `BX[m]`, `instant_train_energy_consumption[KWh]` and `Block`. A row follows for each time step in which the train has a position, with its values in that order, separated by tabs. Between two stretches of steps with a position, the file holds an empty line.

The first line of `TrainPathDiagram.txt` holds `Train/Time` and then the time of every step. Each following line holds the train description, then the position of the train at each of those times, and `-9999` where the train has no position. All values are separated by spaces.

The files of an earlier run stay in a reused output folder. A run without the option neither writes into them nor removes them.

The window has no setting for these files. The option can be given when the application is started with a window as well, because a run in the window uses the same simulation code.
