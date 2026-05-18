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
extern ViewParameters par_view;

struct AgentPartnership {
    bool is_paired = false;
    int partner_id = -1;
    float my_money_share = 1.0f; // 1.0 = 100%
    long last_negotiation_time = 0;
};

static std::map<int, AgentPartnership> agent_partnerships;

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

	// Fixed fuel thresholds: enter fuel-seeking mode below 3.0, exit only after reaching 15.0.
	static const float FUEL_SEEK_ENTER = 3.0f;
	static const float FUEL_SEEK_EXIT  = 15.0f;

	// Per-vehicle sticky fuel-seeking flag.
	static std::map<int, bool> seeking_fuel_map;
	bool& seeking_fuel = seeking_fuel_map[ob->iID];

	if (ob->state.amount_of_fuel <= FUEL_SEEK_ENTER) {
		// Latch on: fuel is critically low.
		if (!seeking_fuel) {
			seeking_fuel = true;
			fprintf(f, "[FUEL] Agent %d entering fuel-seeking mode (fuel=%.2f).\n",
				ob->iID, ob->state.amount_of_fuel);
			sprintf(par_view.inscription2, "FUEL LOW - Agent %d seeking fuel (%.2f)", ob->iID, ob->state.amount_of_fuel);
		}
	} else if (seeking_fuel && ob->state.amount_of_fuel >= FUEL_SEEK_EXIT) {
		// Latch off: fuel is sufficiently restored.
		seeking_fuel = false;
		fprintf(f, "[FUEL] Agent %d leaving fuel-seeking mode (fuel=%.2f).\n",
			ob->iID, ob->state.amount_of_fuel);
		sprintf(par_view.inscription2, "FUEL OK - Agent %d back to coins (%.2f)", ob->iID, ob->state.amount_of_fuel);
	}

	// While seeking fuel, collect barrels exclusively; otherwise collect coins exclusively.
	bool force_barrels = seeking_fuel;
	bool force_coins   = !seeking_fuel;

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

		float kat = atan2(dot_right, dot_fwd);
		ob->state.wheel_turn_angle = -kat;
		if (ob->state.wheel_turn_angle > ob->alpha_max) ob->state.wheel_turn_angle = ob->alpha_max;
		if (ob->state.wheel_turn_angle < -ob->alpha_max) ob->state.wheel_turn_angle = -ob->alpha_max;

		if (dot_fwd < 0) ob->F = -ob->F_max/2;
		else ob->F = ob->F_max;

		if (ob->state.vV.length() > 20) ob->F = 0;
	} else {
		ob->F = 0;
		ob->breaking_degree = 1;
	}

	static long last_transfer_time = clock();

	// remember last observed money/fuel for this local vehicle so we can split recent earnings
	static long last_money_snapshot = -1;

	AgentPartnership& my_partnership = agent_partnerships[ob->iID];

	if (!my_partnership.is_paired) {
		// Look for an unpaired partner
		for (auto it = network_vehicles.begin(); it != network_vehicles.end(); ++it) {
			MovableObject *other = it->second;
			if (other && other->iID != ob->iID) {
				AgentPartnership& other_partnership = agent_partnerships[other->iID];
				
				if (!other_partnership.is_paired && clock() - my_partnership.last_negotiation_time > 2000) {
					// Propose a split based on skills. 
					float total_skills = ob->money_collection_skills + other->money_collection_skills;
					float proposed_my_share = ob->money_collection_skills / (total_skills + 0.001f);
					
					my_partnership.is_paired = true;
					my_partnership.partner_id = other->iID;
					my_partnership.my_money_share = proposed_my_share;
					
					other_partnership.is_paired = true;
					other_partnership.partner_id = ob->iID;
					other_partnership.my_money_share = 1.0f - proposed_my_share;

					fprintf(f, "[NEGOTIATION] Agent %d paired with Agent %d. Split: Agent %d gets %.0f%%, Agent %d gets %.0f%%.\n", 
							ob->iID, other->iID, ob->iID, my_partnership.my_money_share * 100, other->iID, other_partnership.my_money_share * 100);
					sprintf(par_view.inscription2, "PAIRED: Agent %d + Agent %d  |  %d:%.0f%%  %d:%.0f%%",
							ob->iID, other->iID,
							ob->iID, my_partnership.my_money_share * 100,
							other->iID, other_partnership.my_money_share * 100);
					break;
				}
			}
		}
		my_partnership.last_negotiation_time = clock();
	}

	if (last_money_snapshot == -1) last_money_snapshot = ob->state.money;
	
	if (clock() - last_transfer_time > 1000 && ob == my_vehicle) {
		last_transfer_time = clock();
		
		long earned_money = ob->state.money - last_money_snapshot;

		if (earned_money > 0 && my_partnership.is_paired) {
			float partner_share_percent = 1.0f - my_partnership.my_money_share;
			float amount_to_send = earned_money * partner_share_percent;
			
			if (amount_to_send > 0) {
				TransferSending(my_partnership.partner_id, 0 /*MONEY*/, amount_to_send);
				fprintf(f, "[TRANSFER] Agent %d collected %ld money. Sending partner (Agent %d) their %.0f%% cut: %.2f.\n", 
					ob->iID, earned_money, my_partnership.partner_id, partner_share_percent * 100, amount_to_send);
				sprintf(par_view.inscription2, "TRANSFER: Agent %d -> Agent %d  |  %.2f (%.0f%% cut)",
					ob->iID, my_partnership.partner_id, amount_to_send, partner_share_percent * 100);
			}
		}
		
		last_money_snapshot = ob->state.money;
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