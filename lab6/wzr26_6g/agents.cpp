#include <stdlib.h>
#include <time.h>
#include <map>

using namespace std;

#include "agents.h"

extern map<int, MovableObject*> network_vehicles;
extern float TransferSending(int ID_receiver, int transfer_type, float transfer_value);
extern MovableObject *my_vehicle;


AutoPilot::AutoPilot()
{

}

void AutoPilot::AutoControl(MovableObject *ob)
{
	Terrain* teren = ob->terrain;  // wskaŸnik do terenu
	Item* przedmioty = teren->p;   // wskaŸnik do  tablicy przedmiotów

	Vector3 vect_local_forward = ob->state.qOrient.rotate_vector(Vector3(1, 0, 0));
	Vector3 vect_local_right = ob->state.qOrient.rotate_vector(Vector3(0, 0, 1));

	// parametry sterowania:
	ob->breaking_degree = 0;             // si³a hamowania
	ob->F = ob->F_max;                           // si³a napêdowa
	ob->state.wheel_turn_angle = 0;      // k¹t skrêtu kierownicy - mo¿na ustaiwaæ go bezpoœrednio zak³adaj¹c, ¿e robot mo¿e krêciæ kierownic¹ dowolnie szybko,
										 // jednaj gwa³towna zmiana po³o¿enia kierownicy (i tym samym kó³) mo¿e skutkowaæ poœlizgiem pojazdu
	// parametry sterowania daj¹ce wiêkszy realizm zamiast state.wheel_turn_angle:
	ob->wheel_turn_speed = 0;            // prêdkoœæ skrêtu kierownicy (dodatnia - w lewo)
	ob->if_keep_steer_wheel = 0;         // czy kierownica zablokowana (jeœli nie, to wraca do po³o¿enia standardowego)


	// TUTAJ NALE¯Y UMIEŒCIÆ ALGORYTM AUTONOMICZNEGO STEROWANIA POJAZDEM

	float min_dist = 1e10;
	int target_idx = -1;
	for (long i = 0; i < teren->number_of_items; i++) {
		if (przedmioty[i].to_take) {
			if (przedmioty[i].type == ITEM_COIN || przedmioty[i].type == ITEM_BARREL) {
				Vector3 diff = przedmioty[i].vPos - ob->state.vPos;
				float dist = diff.length();
				float weight = 1.0;
				if (przedmioty[i].type == ITEM_COIN) weight /= (ob->money_collection_skills + 0.01);
				if (przedmioty[i].type == ITEM_BARREL) weight /= (ob->fuel_collection_skills + 0.01);

				float score = dist * weight;
				if (score < min_dist) {
					min_dist = score;
					target_idx = i;
				}
			}
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
	if (clock() - last_transfer_time > 1000 && ob == my_vehicle) {
		last_transfer_time = clock();
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
	}


}

void AutoPilot::ControlTest(MovableObject *_ob, float krok_czasowy, float czas_proby)
{
	bool koniec = false;
	float _czas = 0;               // czas liczony od pocz¹tku testu
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

	float pz = 0.1;   // prawdopodobieñstwo zmiany parametru (¿eby nie wszystkie siê zmienia³y ka¿dorazowo) 

	//for (long p=0;p<number_of_params;p++) par[p] = 0.9;
	long gotowka_pop = 0;

	float delta_par[100];
	FILE *f = fopen("wyzarz_log.txt", "w");

	fprintf(f, "Start optymalizacji %d parametrow z wykorzystaniem symulowanego wyzarzania\n", number_of_params);
	for (long ep = 0; ep < number_of_epochs; ep++)
	{
		// losuje poprawki dla czêœci parametrów:
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