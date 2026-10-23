# Data of the bundled cases

The bundled cases in `EGTRAIN/QEGTRAIN/Scenes` have their own sources and terms, which this file records for each case that ships. Where something is not confirmed, it says so.

Every case also has one network-wide signalling area (level 0), added by project decision. It is not repeated below.

## Copenhagen, Milano_Brescia and Paimpol

These three cases come from the dataset *SORTEDMOBILITY: Input data case studies for EGTRAIN rail traffic simulator* on 4TU.ResearchData, by Egidio Quaglietta, Konstantinos Rigos and William Busuttil. The dataset is under CC0 1.0 Universal, which asks for no attribution. The citation at the end is the way to credit it.

### Copenhagen

Source: the Denmark case input, a portion of the Copenhagen suburban network supplied by the infrastructure manager Banedanmark.

Changes for EGTRAIN:

- Two malformed lines of the connections file were repaired.
- Four single-track restrictions were added, at Hellerup (two), Hoje Taastrup and Farum, for the timetable of this case.

### Milano_Brescia

Source: the Italy case input, the line from Pioltello to Rovato supplied by the infrastructure manager RFI.

Changes for EGTRAIN:

- The gradients are divided by 1,000, because the readme of the dataset gives them in per mille. They are stored as rise per length.
- 22 services without a stop are marked as through services.

### Paimpol

Source: the France case input, the single-track line from Guingamp to Paimpol supplied by the infrastructure manager SNCF. Some files differ from the deposit (timetable, train, route and passenger files, among others). They are a later version by the dataset authors, also under CC0 1.0 Universal.

Changes for EGTRAIN: three of the six traction rows of the Draisy train unit are not in the case. The reason is not recorded.

## Netherlands

Source: supplied by Egidio Quaglietta, TU Delft, with the original EGTRAIN.

Terms: it may be redistributed with EGTRAIN. No licence name was given, and none is stated here.

Changes for EGTRAIN: the services end at their last stop.

Attribution: none was specified. The original EGTRAIN is credited in the [README](README.md#credits-and-research-background).

## Assignment_Gvc_Gdg_Ut

Source: the geometry is synthetic and was made for EGTRAIN. The four services and their times come from the 2025 TU Delft course model answers. The source of the values of the train unit is not confirmed.

Terms: the geometry and the four services and times may be redistributed. The terms of the train unit values are not confirmed.

Attribution: none specified.

## Amsterdam_Hilversum_Student

Source: the timetable is fictional and was written by the maintainers of EGTRAIN. The network, the stations and the rolling stock are those of the Netherlands case.

Terms: the timetable is released under CC0 1.0 Universal, which asks for no attribution. The network, stations and rolling stock have the terms of the Netherlands case.

Changes for EGTRAIN: only one service, with four runs. The Netherlands background services are left out. See the [student case guide](docs/guides/amsterdam-hilversum-student-case.md).

## Citation

Quaglietta, E., Rigos, K., Busuttil, W. (2024). *SORTEDMOBILITY: Input data case studies for EGTRAIN rail traffic simulator*, version 1. 4TU.ResearchData. [doi:10.4121/e78d0dc2-3123-4510-a2c5-7ad017a02e33.v1](https://doi.org/10.4121/e78d0dc2-3123-4510-a2c5-7ad017a02e33.v1)
