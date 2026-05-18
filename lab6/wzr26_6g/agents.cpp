#include <stdlib.h>
#include <time.h>
#include <map>
#include <vector>

using namespace std;

#include "agents.h"

extern map<int, MovableObject*> network_vehicles;
extern float TransferSending(int ID_receiver, int transfer_type, float transfer_value);
extern MovableObject *my_vehicle;
extern FILE *f;


AutoPilot::AutoPilot()
{

}

void AutoPilot::AutoControl(MovableObject *ob)
{
	Terrain* teren = ob->terrain;  // wska�nik do terenu
	Item* przedmioty = teren->p;   // wska�nik do  tablicy przedmiot�w

	Vector3 vect_local_forward = ob->state.qOrient.rotate_vector(Vector3(1, 0, 0));
	Vector3 vect_local_right = ob->state.qOrient.rotate_vector(Vector3(0, 0, 1));

	// parametry sterowania:
	ob->breaking_degree = 0;             // si�a hamowania
	ob->F = ob->F_max;                           // si�a nap�dowa
	ob->state.wheel_turn_angle = 0;      // k�t skr�tu kierownicy - mo�na ustaiwa� go bezpo�rednio zak�adaj�c, �e robot mo�e kr�ci� kierownic� dowolnie szybko,
										 // jednaj gwa�towna zmiana po�o�enia kierownicy (i tym samym k�) mo�e skutkowa� po�lizgiem pojazdu
	// parametry sterowania daj�ce wi�kszy realizm zamiast state.wheel_turn_angle:
	ob->wheel_turn_speed = 0;            // pr�dko�� skr�tu kierownicy (dodatnia - w lewo)
	ob->if_keep_steer_wheel = 0;         // czy kierownica zablokowana (je�li nie, to wraca do po�o�enia standardowego)


	// TUTAJ NALE�Y UMIE�CI� ALGORYTM AUTONOMICZNEGO STEROWANIA POJAZDEM

	// Select target item with fuel-aware preferences.
	float min_dist = 1e10;
	int target_idx = -1;

	// determine fuel thresholds using tank_capacity when available
	float low_fuel_threshold = 2.0f;
	float high_fuel_threshold = 50.0f;
	if (ob->tank_capacity > 0) {
		low_fuel_threshold = ob->tank_capacity * 0.2f;
		high_fuel_threshold = ob->tank_capacity * 0.8f;
	}

	// If fuel is low, prefer barrels only. If fuel is very high, prefer coins only.
	bool force_barrels = (ob->state.amount_of_fuel <= low_fuel_threshold);
	bool force_coins = (ob->state.amount_of_fuel >= high_fuel_threshold);

	for (long i = 0; i < teren->number_of_items; i++) {
		if (!przedmioty[i].to_take) continue;
		if (!(przedmioty[i].type == ITEM_COIN || przedmioty[i].type == ITEM_BARREL)) continue;

		if (force_barrels && (przedmioty[i].type != ITEM_BARREL)) continue;
		if (force_coins && (przedmioty[i].type != ITEM_COIN)) continue;

		Vector3 diff = przedmioty[i].vPos - ob->state.vPos;
		float dist = diff.length();
		float score = 1e20f;
		// For coins take into account coin value (e.g., 100 or 200) and own skill -> prefer high value*skill
		if (przedmioty[i].type == ITEM_COIN) {
			float coin_value = przedmioty[i].value;
			float benefit = coin_value * ob->money_collection_skills; // expected reward
			score = dist / (benefit + 0.01f);
		} else if (przedmioty[i].type == ITEM_BARREL) {
			float weight = 1.0f / (ob->fuel_collection_skills + 0.01f);
			score = dist * weight;
		}
		if (score < min_dist) {
			min_dist = score;
			target_idx = i;
		}
	}

	if (target_idx != -1) {
		Vector3 diff = przedmioty[target_idx].vPos - ob->state.vPos;
		float dot_right = diff ^ vect_local_right;
		float dot_fwd = diff ^ vect_local_forward;

		// If there's a tree roughly between us and the target, attempt a detour by turning
		float dist_to_target = diff.length();
		bool blocked_by_tree = false;
		for (long ti = 0; ti < teren->number_of_items; ti++) {
			if (przedmioty[ti].type != ITEM_TREE) continue;
			Vector3 treeDiff = przedmioty[ti].vPos - ob->state.vPos;
			float dTree = treeDiff.length();
			if (dTree <= 0.01f || dTree >= dist_to_target) continue;
			// angle between forward and tree
			float angle_tree = atan2((treeDiff ^ vect_local_right), (treeDiff ^ vect_local_forward));
			float angle_target = atan2((diff ^ vect_local_right), (diff ^ vect_local_forward));
			// if tree is roughly in front (within ~22 degrees) and near the line to target, consider it blocking
			if (fabs(angle_tree) < (22.0f * PI / 180.0f) && fabs(angle_tree - angle_target) < (30.0f * PI / 180.0f)) {
				blocked_by_tree = true;
				// steer around: pick direction away from tree lateral position
				float turn_dir = (treeDiff ^ vect_local_right) > 0 ? -1.0f : 1.0f; // if tree on right, turn left
				ob->state.wheel_turn_angle = turn_dir * ob->alpha_max * 0.8f;
				ob->F = ob->F_max * 0.5f; // slow while turning
				break;
			}
		}
		if (!blocked_by_tree) {

		float kat = atan2(dot_right, dot_fwd);
		ob->state.wheel_turn_angle = -kat;
		if (ob->state.wheel_turn_angle > ob->alpha_max) ob->state.wheel_turn_angle = ob->alpha_max;
		if (ob->state.wheel_turn_angle < -ob->alpha_max) ob->state.wheel_turn_angle = -ob->alpha_max;

		if (dot_fwd < 0) ob->F = -ob->F_max/2;
		else ob->F = ob->F_max;

		if (ob->state.vV.length() > 20) ob->F = 0;
		}
	} else {
		ob->F = 0;
		ob->breaking_degree = 1;
	}

	static long last_transfer_time = clock();

	// remember last observed money/fuel for this local vehicle so we can split recent earnings
	static long last_money_snapshot = -1;
	static float last_fuel_snapshot = -1.0f;

	// cooperation radius (meters) to consider partners who helped collect an item
	const float COOP_RADIUS = 50.0f;

	if (last_money_snapshot == -1) last_money_snapshot = ob->state.money;
	if (last_fuel_snapshot < 0) last_fuel_snapshot = ob->state.amount_of_fuel;
	if (clock() - last_transfer_time > 1000 && ob == my_vehicle) {
		last_transfer_time = clock();
		// legacy pairwise balancing (keeps totals balanced between pairs)
		for (auto it = network_vehicles.begin(); it != network_vehicles.end(); ++it) {
			MovableObject *other = it->second;
			if (other && other->iID != ob->iID) {
				float total_money = ob->state.money + other->state.money;
				float my_target_money = total_money * (ob->money_collection_skills / (ob->money_collection_skills + other->money_collection_skills));
				if (ob->state.money > my_target_money + 1.0f) {
					TransferSending(other->iID, 0 /*MONEY*/, ob->state.money - my_target_money);
				}

				float total_fuel = ob->state.amount_of_fuel + other->state.amount_of_fuel;
				float my_target_fuel = total_fuel * (ob->fuel_collection_skills / (ob->fuel_collection_skills + other->fuel_collection_skills));
				if (ob->state.amount_of_fuel > my_target_fuel + 0.1f) {
					TransferSending(other->iID, 1 /*FUEL*/, ob->state.amount_of_fuel - my_target_fuel);
				}
			}
		}

		// ----- New: split recent earnings among nearby cooperating cars proportionally to their skills -----
		long earned_money = ob->state.money - last_money_snapshot;
		float earned_fuel = ob->state.amount_of_fuel - last_fuel_snapshot;

		if (earned_money > 0) {
			// find nearby partners (including those in network_vehicles within COOP_RADIUS)
			float sum_skills = ob->money_collection_skills;
			vector<MovableObject*> partners;
			partners.push_back(ob);
			for (auto it2 = network_vehicles.begin(); it2 != network_vehicles.end(); ++it2) {
				MovableObject *other = it2->second;
				if (!other || other->iID == ob->iID) continue;
				float d = (other->state.vPos - ob->state.vPos).length();
				if (d <= COOP_RADIUS) {
					partners.push_back(other);
					sum_skills += other->money_collection_skills;
				}
			}

			if (partners.size() > 1 && sum_skills > 0.0f) {
				fprintf(f, "Splitting recent earning %.2f among %zu partners (skill sum=%.3f)\n", (double)earned_money, partners.size(), (double)sum_skills);
				// send each partner their proportional share (except self)
				float total_sent = 0.0f;
				for (size_t p = 0; p < partners.size(); ++p) {
					MovableObject *m = partners[p];
					if (m->iID == ob->iID) continue; // local keeps its portion until we send others
					float share = earned_money * (m->money_collection_skills / sum_skills);
					if (share <= 0) continue;
					TransferSending(m->iID, 0 /*MONEY*/, share);
					total_sent += share;
					fprintf(f, "  -> to ID %d share=%.2f (skill=%.3f)\n", m->iID, (double)share, (double)m->money_collection_skills);
				}
				// adjust last snapshot: after sending money has been deducted by TransferSending
				last_money_snapshot = ob->state.money;
			} else {
				// no partners -> just update snapshot
				last_money_snapshot = ob->state.money;
			}
		} else {
			// if money decreased or unchanged, update snapshot to current (prevents re-trigger)
			last_money_snapshot = ob->state.money;
		}

		if (earned_fuel > 0.001f) {
			float sum_skills_f = ob->fuel_collection_skills;
			vector<MovableObject*> partners_f;
			partners_f.push_back(ob);
			for (auto it2 = network_vehicles.begin(); it2 != network_vehicles.end(); ++it2) {
				MovableObject *other = it2->second;
				if (!other || other->iID == ob->iID) continue;
				float d = (other->state.vPos - ob->state.vPos).length();
				if (d <= COOP_RADIUS) {
					partners_f.push_back(other);
					sum_skills_f += other->fuel_collection_skills;
				}
			}

			if (partners_f.size() > 1 && sum_skills_f > 0.0f) {
				fprintf(f, "Splitting recent fuel gain %.2f among %zu partners (skill sum=%.3f)\n", (double)earned_fuel, partners_f.size(), (double)sum_skills_f);
				for (size_t p = 0; p < partners_f.size(); ++p) {
					MovableObject *m = partners_f[p];
					if (m->iID == ob->iID) continue;
					float share = earned_fuel * (m->fuel_collection_skills / sum_skills_f);
					if (share <= 0) continue;
					TransferSending(m->iID, 1 /*FUEL*/, share);
					fprintf(f, "  -> to ID %d fuel_share=%.2f (skill=%.3f)\n", m->iID, (double)share, (double)m->fuel_collection_skills);
				}
				last_fuel_snapshot = ob->state.amount_of_fuel;
			} else {
				last_fuel_snapshot = ob->state.amount_of_fuel;
			}
		} else {
			last_fuel_snapshot = ob->state.amount_of_fuel;
		}
	}


}

void AutoPilot::ControlTest(MovableObject *_ob, float krok_czasowy, float czas_proby)
{
	bool koniec = false;
	float _czas = 0;               // czas liczony od pocz�tku testu
	//FILE *pl = fopen("test_sterowania.txt","w");
	while (!koniec)
	{
		_ob->Simulation(krok_czasowy);
		AutoControl(_ob);
		_czas += krok_czasowy;
		if (_czas >= czas_proby) koniec = true;
		//fprintf(pl,"czas %f, vPos[%f %f %f], got %d, pal %f, F %f, wheel_turn_angle %f, breaking_degree %f\n",_czas,_ob->vPos.x,_ob->vPos.y,_ob->vPos.z,_ob->money,_ob->amount_of_fuel,_ob->F,_ob->wheel_turn_angle,_ob->breaking_degree);
	}
	//fclose(pl);
}

// losowanie liczby z rozkladu normalnego o zadanej sredniej i wariancji
float Randn(float srednia, float wariancja, long liczba_iter)
{
	//long liczba_iter = 10;  // im wiecej iteracji tym rozklad lepiej przyblizony
	float suma = 0;
	for (long i = 0; i < liczba_iter; i++)
		suma += (float)rand() / RAND_MAX;
	return (suma - (float)liczba_iter / 2)*sqrt(12 * wariancja / liczba_iter) + srednia;
}

void AutoPilot::ParametersSimAnnealing(long number_of_epochs, float krok_czasowy, float czas_proby)
{
	float T = 0.02,//100,
		wT = 0.99,
		c = 100000.0;

	float pz = 0.1;   // prawdopodobie�stwo zmiany parametru (�eby nie wszystkie si� zmienia�y ka�dorazowo) 

	//for (long p=0;p<number_of_params;p++) par[p] = 0.9;
	long gotowka_pop = 0;

	float delta_par[100];
	FILE *f = fopen("wyzarz_log.txt", "w");

	fprintf(f, "Start optymalizacji %d parametrow z wykorzystaniem symulowanego wyzarzania\n", number_of_params);
	for (long ep = 0; ep < number_of_epochs; ep++)
	{
		// losuje poprawki dla cz�ci parametr�w:
		for (long p = 0; p < number_of_params; p++)
			if ((float)rand() / RAND_MAX < pz)
				delta_par[p] = Randn(0, T, 10);
			else
				delta_par[p] = 0;

		if (ep > 0)
			for (long p = 0; p < number_of_params; p++)
				par[p] += delta_par[p];
		for (long i = 0; i < number_of_params; i++)
			fprintf(f, "par[%d] = %3.10f;\n", i, par[i]);
		Terrain t2;
		MovableObject *Obiekt = new MovableObject(&t2);
		Obiekt->planting_skills = 1.0;
		Obiekt->money_collection_skills = 1.0;
		Obiekt->fuel_collection_skills = 1.0;
		long gotowka_pocz = Obiekt->state.money;

		ControlTest(Obiekt, krok_czasowy, czas_proby);

		long gotowka = Obiekt->state.money - gotowka_pocz;

		float dE = gotowka - gotowka_pop;
		float p_akc = 1.0 / (1 + exp(-dE / (c*T)));
		fprintf(f, "epoka %d: T = %f, gotowka = %d, dE = %f, p_akc = %f\n", ep, T, gotowka, dE, p_akc);
		//if (gotowka > 15000) break;
		// akceptujemy lub odrzucamy
		if (((float)rand() / RAND_MAX < p_akc) || (ep == 0))
		{
			gotowka_pop = gotowka;

			fprintf(f, "sym.wyz-akceptacja, %d epoka: T=%f, gotowka = %d\n", ep, T, gotowka);
			char lanc[256];
			sprintf(lanc, "sym.wyz-akceptacja, %d epoka: T=%f, gotowka = %d", ep, T, gotowka);
			//SetWindowText(main_window, lanc);

		}
		else
		{
			for (long p = 0; p < number_of_params; p++) par[p] -= delta_par[p];
		}
		delete Obiekt;
		fclose(f);
		f = fopen("wyzarz_log.txt", "a");

		T *= wT;

	} // po epokach

	fprintf(f, "Koniec wyzarzania, koncowy wynik to %d gotowki\nOto koncowe wartosci:\n", gotowka_pop);
	for (long i = 0; i < number_of_params; i++)
		fprintf(f, "par[%d] = %3.10f;\n", i, par[i]);
	fclose(f);

}