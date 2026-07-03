#include "initialParameters.h"

initial_parameters::initial_parameters(int case_study) {

	switch (case_study) {
		case 0:
			break;
		case 1:								// Netherlands case
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			GUI = 1;
			times = 8000;
			numTrackLines = 268;
			N_Routes = 74;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			TSM = 0;
			RChoice = 0;
			InputMainFolder = "Input/Input_EGTRAIN_Netherlands";
			OutputMainFolder = "Output/Output_EGTRAIN_Netherlands";
			break;
		case 2:
			startingSimulationTime = 25840; // 25200; 23300=>06:28:20  // 25800 => 07:10:00
			GUI = 1;
			times = 9000;
			numTrackLines = 6;
			N_Routes = 14;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			TSM = 0;
			RChoice = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Paimpol";
			OutputMainFolder = "Output/Output_EGTRAIN_Paimpol";
			break;

		case 3:
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			GUI = 0;
			times = 8000;
			numTrackLines = 30;
			N_Routes = 24;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Banedanmark";
			OutputMainFolder = "Output/Output_EGTRAIN_Banedanmark";
			TSM = 0;
			RChoice = 0;
			Service_lines = {"A", "B", "Bx", "C", "E", "F", "H", "common"};
			// Service_lines2 = { "A&E","B&Bx","C&H","F"};

			Line_stations["A"] = {"Hillerod", "Allerod", "Birkerod", "Holte", "Lyngby", "Hellerup", "Svanemollen", "Nordhavn",
								  "Osterport", "Norreport", "Vesterport", "KobenhavnH", "Dybbolsbro", "Sydhavn", "Sjaelor", "NyEllebjergAE", "Amarken",
								  "Friheden", "Avedore", "BrondbyStrand", "Vallensbaek", "Ishoj", "Hundige"};
			Line_stations["B"] = {"Farum", "Vaerlose", "Hareskov", "Skovbrynet", "Bagsvaerd", "Stengarden", "Buddinge", "Kildebakke",
								  "Vangede", "Dyssegard", "Emdrup", "RyparkenBBx", "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport",
								  "KobenhavnH", "Dybbolsbro", "Carlsberg", "Valby", "DanshojBBx", "Hvidovre", "Rodovre", "Brondbyoster", "Glostrup",
								  "Albertslund", "Taastrup", "HojeTaastrup"};
			Line_stations["Bx"] = {"BuddingeReverse", "Buddinge", "Kildebakke", "Vangede", "Dyssegard", "Emdrup", "RyparkenBBx",
								   "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport", "KobenhavnH",
								   "Dybbolsbro", "Carlsberg", "Valby", "DanshojBBx", "Glostrup", "Albertslund", "Taastrup",
								   "HojeTaastrup"};
			Line_stations["C"] = {"Frederikssund", "Vinge", "Olstykke", "Egedal", "Stenlose", "Vekso", "Kildedal", "Malov",
								  "Ballerup", "Malmparken", "Skovlunde", "Herlev", "Husum", "Vanlose", "FlintholmCH", "Valby", "Carlsberg",
								  "Dybbolsbro", "KobenhavnH", "Vesterport", "Norreport", "Osterport", "Nordhavn", "Svanemollen", "Hellerup",
								  "Charlottenlund", "Ordrup", "Klampenborg"};
			Line_stations["E"] = {"Holte", "Virum", "Sorgenfri", "Lyngby", "Jaegersborg", "Gentofte", "Bernstorffsvej", "Hellerup",
								  "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport", "KobenhavnH", "Dybbolsbro", "Sydhavn",
								  "Sjaelor", "NyEllebjergAE", "Ishoj", "Hundige", "Greve", "Karlslunde", "SolrodStrand", "Jersie", "KogeNord",
								  "Olby", "Koge"};
			Line_stations["F"] = {"HellerupStorage", "Hellerup", "RyparkenF", "Bispebjerg", "Norrebro", "Fuglebakken",
								  "Grondal", "FlintholmF", "KBHallen", "Alholm", "DanshojF", "VigerslevAlle", "NyEllebjergF"};
			Line_stations["H"] = {"BallerupStorage", "Ballerup", "Malmparken", "Skovlunde", "Herlev", "Husum", "Islev",
								  "Jyllingevej", "Vanlose", "FlintholmCH", "PeterBangsvej", "Langgade", "Valby", "Carlsberg", "Dybbolsbro",
								  "KobenhavnH", "Vesterport", "Norreport", "OsterportH"};

			Line_stations["common"] = {"Valby", "Carlsberg", "Dybbolsbro", "KobenhavnH", "Vesterport", "Norreport", "Osterport", "Nordhavn", "Svanemollen", "Hellerup"};
			break;

		case 4: // MILANO-BRESCIA line
			GUI = 1;
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			times = 4000;
			numTrackLines = 38;
			N_Routes = 48;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			TSM = 0;
			RChoice = 0;
			InputMainFolder = "Input/Input_EGTRAIN_Milano_Brescia";
			OutputMainFolder = "Output/Output_EGTRAIN_Milano_Brescia";
			break;

		case 5: // Assignment corridor Gvc-Gdg-Ut
			GUI = 0;
			startingSimulationTime = 25200; // 07:00:00
			times = 10000;
			numTrackLines = 2;
			N_Routes = 2;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			TSM = 0;
			RChoice = 0;
			InputMainFolder = "Input/Input_EGTRAIN_Assignment";
			OutputMainFolder = "Output/Output_EGTRAIN_Assignment";
			break;
		default:
			std::cout << "No case selected";
	};
}

void initial_parameters::set_case(int case_study) {

	switch (case_study) {
		case 1: // Netherlands case
			std::cout << "\nSelected Case study: " << case_study << '\n';
			name = "Netherlands";
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			GUI = 1;
			times = 8000;
			numTrackLines = 268;
			N_Routes = 74;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Netherlands";
			OutputMainFolder = "Output/Output_EGTRAIN_Netherlands";
			break;
		case 2:
			std::cout << "\nSelected Case study: " << case_study << '\n';
			name = "Paimpol";
			startingSimulationTime = 25840; // 25840 => 07:10:40; 23300=>06:28:20
			// GUI = 1;
			times = 9000;
			numTrackLines = 6;
			N_Routes = 15;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Paimpol";
			OutputMainFolder = "Output/Output_EGTRAIN_Paimpol";
			TSM = 0;
			RChoice = 0;
			break;

		case 3:
			std::cout << "\nSelected Case study: " << case_study << '\n';
			name = "Copenhagen";
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			// GUI = 1;
			times = 8000;
			numTrackLines = 168;
			N_Routes = 24;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Banedanmark";
			OutputMainFolder = "Output/Output_EGTRAIN_Banedanmark";
			TSM = 0;
			RChoice = 0;

			Service_lines = {"A", "B", "Bx", "C", "E", "F", "H"};
			// Service_lines2 = { "A&E","B&Bx","C&H","F"};

			Line_stations["A"] = {"Hillerod", "Allerod", "Birkerod", "Holte", "Lyngby", "Hellerup", "Svanemollen", "Nordhavn",
								  "Osterport", "Norreport", "Vesterport", "KobenhavnH", "Dybbolsbro", "Sydhavn", "Sjaelor", "NyEllebjergAE", "Amarken",
								  "Friheden", "Avedore", "BrondbyStrand", "Vallensbaek", "Ishoj", "Hundige"};
			Line_stations["B"] = {"Farum", "Vaerlose", "Hareskov", "Skovbrynet", "Bagsvaerd", "Stengarden", "Buddinge", "Kildebakke",
								  "Vangede", "Dyssegard", "Emdrup", "RyparkenBBx", "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport",
								  "KobenhavnH", "Dybbolsbro", "Carlsberg", "Valby", "DanshojBBx", "Hvidovre", "Rodovre", "Brondbyoster", "Glostrup",
								  "Albertslund", "Taastrup", "HojeTaastrup"};
			Line_stations["Bx"] = {"BuddingeReverse", "Buddinge", "Kildebakke", "Vangede", "Dyssegard", "Emdrup", "RyparkenBBx",
								   "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport", "KobenhavnH",
								   "Dybbolsbro", "Carlsberg", "Valby", "DanshojBBx", "Glostrup", "Albertslund", "Taastrup",
								   "HojeTaastrup"};
			Line_stations["C"] = {"Frederikssund", "Vinge", "Olstykke", "Egedal", "Stenlose", "Vekso", "Kildedal", "Malov",
								  "Ballerup", "Malmparken", "Skovlunde", "Herlev", "Husum", "Vanlose", "FlintholmCH", "Valby", "Carlsberg",
								  "Dybbolsbro", "KobenhavnH", "Vesterport", "Norreport", "Osterport", "Nordhavn", "Svanemollen", "Hellerup",
								  "Charlottenlund", "Ordrup", "Klampenborg"};
			Line_stations["E"] = {"Holte", "Virum", "Sorgenfri", "Lyngby", "Jaegersborg", "Gentofte", "Bernstorffsvej", "Hellerup",
								  "Svanemollen", "Nordhavn", "Osterport", "Norreport", "Vesterport", "KobenhavnH", "Dybbolsbro", "Sydhavn",
								  "Sjaelor", "NyEllebjergAE", "Ishoj", "Hundige", "Greve", "Karlslunde", "SolrodStrand", "Jersie", "KogeNord",
								  "Olby", "Koge"};
			Line_stations["F"] = {"HellerupStorage", "Hellerup", "RyparkenF", "Bispebjerg", "Norrebro", "Fuglebakken",
								  "Grondal", "FlintholmF", "KBHallen", "Alholm", "DanshojF", "VigerslevAlle", "NyEllebjergF"};
			Line_stations["H"] = {"BallerupStorage", "Ballerup", "Malmparken", "Skovlunde", "Herlev", "Husum", "Islev",
								  "Jyllingevej", "Vanlose", "FlintholmCH", "PeterBangsvej", "Langgade", "Valby", "Carlsberg", "Dybbolsbro",
								  "KobenhavnH", "Vesterport", "Norreport", "OsterportH"};

			Line_stations["common"] = {"Valby", "Carlsberg", "Dybbolsbro", "KobenhavnH", "Vesterport", "Norreport", "Osterport", "Nordhavn", "Svanemollen", "Hellerup"};
			break;

		case 4: // MILANO-BRESCIA line
			std::cout << "\nSelected Case study: " << case_study << '\n';
			name = "Brescia";
			GUI = 1;
			startingSimulationTime = 23300; // 25200; 23300=>06:28:20
			times = 4000;
			numTrackLines = 38;
			N_Routes = 48;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Milano_Brescia";
			OutputMainFolder = "Output/Output_EGTRAIN_Milano_Brescia";
			TSM = 0;
			RChoice = 0;
			break;

		case 5: // Assignment corridor Gvc-Gdg-Ut
			std::cout << "\nSelected Case study: " << case_study << '\n';
			name = "Assignment";
			GUI = 0;
			startingSimulationTime = 25200; // 07:00:00
			times = 10000;
			numTrackLines = 2;
			N_Routes = 2;
			bufferTime = 0;
			recoveryTimePercentage = 0;
			num_OrderLists = 1;
			InputMainFolder = "Input/Input_EGTRAIN_Assignment";
			OutputMainFolder = "Output/Output_EGTRAIN_Assignment";
			TSM = 0;
			RChoice = 0;
			break;
		default:
			std::cout << "No case selected";
	};
}
