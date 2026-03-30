/****************************************************
	Virtual Collaborative Teams - The base program 
    The main module
****************************************************/

bool if_prediction_test = true;
bool if_delays = false;
bool if_shadow = true;
// Scenariusz testu predykcji - tzw. benchmark - dzięki temu można porównać różne algorytmy predykcji na tym samym scenariuszu:
// {czas [s], siła [N], prędkość skręcania kół [rad/s], stopień hamowania} -> przez jaki czas obiekt ma sie poruszać z podaną prędkością i kątem skrętu kół
float test_scenario[][4] = { { 6.5, 200, 0, 0 }, { 4, 90, -0.50 / 8, 0 }, { 0.5, 0, 0, 1.0 }, { 4, 80, 1.00 / 8, 0 }, { 5, 200, 0, 0 } };
//float test_scenario[][4] = { { 9.0, 500, 0, 0 }, { 10, -200, -0.25 / 2, 0 } };  // scenariusz ekstremalny


#include <windows.h>
#include <math.h>
#include <time.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include <iterator> 
#include <map>

#include "objects.h"
#include "graphics.h"
#include "net.h"
using namespace std;

FILE *f = fopen("wzr_opis.txt", "a"); // plik do zapisu informacji testowych


MovableObject *my_vehicle;               // obiekt przypisany do tej aplikacji
Terrain terrain;


map<int, MovableObject*> other_vehicles;

float avg_cycle_time;                // sredni czas pomiedzy dwoma kolejnymi cyklami symulacji i wyswietlania
long time_of_cycle, number_of_cyc=0;   // zmienne pomocnicze potrzebne do obliczania avg_cycle_time
long time_start = clock();           // moment uruchomienia aplikacji 
long time_last_send = 0;             // moment wysłania ostatniej ramki  

multicast_net *multi_reciv;          // wsk do obiektu zajmujacego sie odbiorem komunikatow
multicast_net *multi_send;           //   -||-  wysylaniem komunikatow

HANDLE threadReciv;                  // uchwyt wątku odbioru komunikatów
HWND window_handle;                    // uchwyt do głównego okna programu 
CRITICAL_SECTION m_cs;               // do synchronizacji wątków

bool if_SHIFT_pressed = false;
bool if_ID_visible = true;           // czy rysowac nr ID przy każdym obiekcie
bool if_mouse_control = false;       // sterowanie za pomocą klawisza myszki
int mouse_cursor_x = 0, mouse_cursor_y = 0;     // położenie kursora myszy

extern ViewParams view_parameters;           // ustawienia widoku zdefiniowane w grafice

long time_of_day = 1000;         // czas trwania dnia w [s]

// zmienne związane z nawigacją obliczeniową:
long number_of_send_trials = 0;        // liczba prób wysylania ramki ze stanem  
float sum_differences_of_pos = 0;             // sumaryczna odległość pomiędzy położeniem rzeczywistym (symulowanym) a ekstrapolowanym
float sum_of_angle_differences = 0;             // sumaryczna różnica kątowa -||- 


struct Frame                                      // główna struktura służąca do przesyłania informacji
{	
	int iID;                                      // identyfikator obiektu, którego 
	int type;                                     // typ ramki: informacja o stateie, informacja o zamknięciu, komunikat tekstowy, ... 
	ObjectState state;                            // położenie, prędkość: środka masy + kątowe, ...

	long sending_time;                            // tzw. znacznik czasu potrzebny np. do obliczenia opóźnienia
	int iID_receiver;                             // nr ID odbiorcy wiadomości, jeśli skierowana jest tylko do niego
};


//******************************************
// Funkcja obsługi wątku odbioru komunikatów 
// UWAGA!  Odbierane są też komunikaty z własnej aplikacji by porównać obraz ekstrapolowany do rzeczywistego.
DWORD WINAPI ReceiveThreadFun(void *ptr)
{
	multicast_net *pmt_net = (multicast_net*)ptr;  // wskaźnik do obiektu klasy multicast_net
	Frame frame;

	while (1)
	{
		int frame_size = pmt_net->reciv((char*)&frame, sizeof(Frame));   // oczekiwanie na nadejście ramki 
		ObjectState state = frame.state;

		//fprintf(f, "odebrano stan iID = %d, ID dla mojego obiektu = %d\n", frame.iID, my_vehicle->iID);

		// Lock the Critical section
		EnterCriticalSection(&m_cs);               // wejście na ścieżkę krytyczną - by inne wątki (np. główny) nie współdzielił 
	                                               // tablicy other_vehicles

		if ((if_shadow) || (frame.iID != my_vehicle->iID))          // jeśli to nie mój własny obiekt
		{
			
			if ((other_vehicles.size() == 0) || (other_vehicles[frame.iID] == NULL))        // nie ma jeszcze takiego obiektu w tablicy -> trzeba go
				// stworzyć
			{
				MovableObject *ob = new MovableObject();
				ob->iID = frame.iID;
				other_vehicles[frame.iID] = ob;		
				//fprintf(f, "zarejestrowano %d obcy obiekt o ID = %d\n", iLiczbaCudzychOb - 1, CudzeObiekty[iLiczbaCudzychOb]->iID);
			}
			other_vehicles[frame.iID]->ChangeState(state);   // aktualizacja stateu obiektu obcego 	
			
		}	
		//Release the Critical section
		LeaveCriticalSection(&m_cs);               // wyjście ze ścieżki krytycznej
	}  // while(1)
	return 1;
}

// *****************************************************************
// ****    Wszystko co trzeba zrobić podczas uruchamiania aplikacji
// ****    poza grafiką   
void InteractionInitialization()
{
	DWORD dwThreadId;

	my_vehicle = new MovableObject();    // tworzenie wlasnego obiektu

	time_of_cycle = clock();             // pomiar aktualnego czasu

	// obiekty sieciowe typu multicast (z podaniem adresu WZR oraz numeru portu)
	multi_reciv = new multicast_net("224.12.13.189", 10001);      // obiekt do odbioru ramek sieciowych
	multi_send = new multicast_net("224.12.13.189", 10001);       // obiekt do wysyłania ramek


	// uruchomienie wątku obsługującego odbiór komunikatów:
	threadReciv = CreateThread(
		NULL,                        // no security attributes
		0,                           // use default stack size
		ReceiveThreadFun,                // thread function
		(void *)multi_reciv,               // argument to thread function
		NULL,                        // use default creation flags
		&dwThreadId);                // returns the thread identifier
	SetThreadPriority(threadReciv, THREAD_PRIORITY_HIGHEST);

	fprintf(f,"poczatek interakcji\n");
}


// *****************************************************************
// ****    Wszystko co trzeba zrobić w każdym cyklu działania 
// ****    aplikacji poza grafiką 
void VirtualWorldCycle()
{
	number_of_cyc++;
	float time_from_start_in_s = (float)(clock() - time_start) / CLOCKS_PER_SEC;  // czas w sek. jaki upłynął od uruchomienia programu

	if (number_of_cyc % 50 == 0)          // jeśli licznik cykli przekroczył pewną wartość, to
	{                              // należy na nowo obliczyć średni czas cyklu avg_cycle_time
		char text[256];
		long prev_time = time_of_cycle;
		time_of_cycle = clock();
		float fFps = (50 * CLOCKS_PER_SEC) / (float)(time_of_cycle - prev_time);
		if (fFps != 0) avg_cycle_time = 1.0 / fFps; else avg_cycle_time = 1;
	
		sprintf(text, "WZR 2025/26, tem.3, wersja g, częstość pr.wys. = %0.2f[r/s]  śr.odl = %0.3f[m]  śr.rożn.kąt. = %0.3f[st]",
			(float)number_of_send_trials / time_from_start_in_s, sum_differences_of_pos / number_of_cyc, 
			sum_of_angle_differences / number_of_cyc*180.0 / 3.14159);

		if (time_from_start_in_s > 5)
			SetWindowText(window_handle, text); // wyświetlenie aktualnych odchyłek						
	}

	// obliczenie średniej odległości i średniej różnicy kątowej pomiędzy pojazdem a cieniem:
	EnterCriticalSection(&m_cs);
	MovableObject *car = (other_vehicles.size() > 0 ? other_vehicles[my_vehicle->iID] : NULL);       
	if (car != NULL)
	{
		sum_differences_of_pos += DistanceBetweenPointsOnTetraMap(my_vehicle->state.vPos, car->state.vPos);
		sum_of_angle_differences += AngleBetweenQuats(my_vehicle->state.qOrient, car->state.qOrient);
	}
	else {
		sum_differences_of_pos += DistanceBetweenPointsOnTetraMap(my_vehicle->state.vPos, Vector3(0,0,0));  
		sum_of_angle_differences += AngleBetweenQuats(my_vehicle->state.qOrient, quaternion(0, 0, 0, 1));
	}
	LeaveCriticalSection(&m_cs);
	
	// test predykcji:
	if (if_prediction_test)
	{
		int number_of_actions = sizeof(test_scenario) / (4 * sizeof(float));
		bool test_finished = test_scenario_step(my_vehicle, test_scenario, number_of_actions, time_from_start_in_s);

		if (test_finished) // czas dobiegl konca -> koniec testu 
		{
			if_prediction_test = false;
			char text[200];
			sprintf(text, "Po czasie %3.2f[s]  śr.częstość = %0.2f[r/s]  śr.odl = %0.3f[m]  śr.różn.kąt. = %0.3f[st]",
				time_from_start_in_s, (float)number_of_send_trials / time_from_start_in_s, 
				sum_differences_of_pos / number_of_cyc, sum_of_angle_differences / number_of_cyc*180.0 / 3.14159);
			fprintf(f, "%s\n", text);
			MessageBox(window_handle, text, "Test predykcji", MB_OK);
		}
	}

	my_vehicle->Simulation(avg_cycle_time);                    // symulacja własnego obiektu

	time_from_start_in_s = (float)(clock() - time_start) / CLOCKS_PER_SEC;
	
	//if ((float)(clock() - time_last_send) / CLOCKS_PER_SEC >= 0.2 + 30 * (time_from_start_in_s < 30))
	if ((float)(clock() - time_last_send) / CLOCKS_PER_SEC >= 1.0)
	{
		Frame frame;
		frame.state = my_vehicle->State();                   // stan własnego obiektu 
		frame.iID = my_vehicle->iID;
		multi_send->send((char*)&frame, sizeof(Frame));  // wysłanie komunikatu do pozostałych aplikacji co pewien czas
		time_last_send = clock();
		number_of_send_trials++;
	}

	// ---------------------------------------------------------------
	// ---------------------------------------------------------------
	// ---------------------------------------------------------------
	// ------------  Miejsce na predykcję stanu:  --------------------
	// Lock the Critical section
	EnterCriticalSection(&m_cs);
	for (map<int, MovableObject*>::iterator it = other_vehicles.begin(); it != other_vehicles.end(); ++it)
	{
		MovableObject *veh = it->second;

		if (veh)
		{
			float dt_pred = avg_cycle_time;

			dt_pred = max(0, dt_pred);

			veh->state.vPos = veh->state.vPos + veh->state.vV * dt_pred + (veh->state.vA * dt_pred * dt_pred)/2;

			veh->state.vV = veh->state.vV + veh->state.vA * dt_pred;

			Vector3 d_angle = veh->state.vV_ang * dt_pred + (veh->state.vA_ang * dt_pred * dt_pred)/2;
			float angle = d_angle.length();

			if (angle > 1e-6)
			{
				veh->state.qOrient = (AsixToQuat(d_angle / angle, angle) * veh->state.qOrient).n();
			}
			
			veh->state.vV_ang = veh->state.vV_ang + veh->state.vA_ang * dt_pred;

			float y_ground = terrain.DistFromGround(veh->state.vPos.x, veh->state.vPos.z) + veh->height / 2 + veh->clearance;
			if (veh->state.vPos.y < y_ground)
			{
				veh->state.vPos.y = y_ground;
				if (veh->state.vV.y < 0) veh->state.vV.y = 0;
				if (veh->state.vA.y < 0) veh->state.vA.y = 0;
			}
		}

	}
	//Release the Critical section
	LeaveCriticalSection(&m_cs);
}

// *****************************************************************
// ****    Wszystko co trzeba zrobić podczas zamykania aplikacji
// ****    poza grafiką 
void EndOfInteraction()
{
	fprintf(f, "Koniec interakcji\n");
	fclose(f);
}

//deklaracja funkcji obslugi okna
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

HDC g_context = NULL;        // uchwyt contextu graficznego



//funkcja Main - dla Windows
int WINAPI WinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR     lpCmdLine,
	int       nCmdShow)
{
	
	//Initilize the critical section
	InitializeCriticalSection(&m_cs);

	MSG message;		  //innymi slowy "komunikat"
	WNDCLASS main_class; //klasa głównego okna aplikacji

	static char class_name[] = "Jakas_klasa";

	//Definiujemy klase głównego okna aplikacji
	//Okreslamy tu wlasciwosci okna, szczegoly wygladu oraz
	//adres funkcji przetwarzajacej komunikaty
	main_class.style = CS_HREDRAW | CS_VREDRAW;
	main_class.lpfnWndProc = WndProc; //adres funkcji realizującej przetwarzanie meldunków 
	main_class.cbClsExtra = 0;
	main_class.cbWndExtra = 0;
	main_class.hInstance = hInstance; //identyfikator procesu przekazany przez MS Windows podczas uruchamiania programu
	main_class.hIcon = 0;
	main_class.hCursor = LoadCursor(0, IDC_ARROW);
	main_class.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
	main_class.lpszMenuName = "Menu";
	main_class.lpszClassName = class_name;

	//teraz rejestracja klasy okna głównego
	RegisterClass(&main_class);

	window_handle = CreateWindow(class_name, "WZR 2025/26 temat 3, wersja g", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		200, 50, 820, 690, NULL, NULL, hInstance, NULL);

	ShowWindow(window_handle, nCmdShow);

	//odswiezamy zawartosc okna
	UpdateWindow(window_handle);

	// pobranie komunikatu z kolejki jeśli funkcja PeekMessage zwraca wartość inną niż FALSE,
	// w przeciwnym wypadku symulacja wirtualnego świata wraz z wizualizacją
	ZeroMemory(&message, sizeof(message));
	while (message.message != WM_QUIT)
	{
		if (PeekMessage(&message, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessage(&message);
		}
		else
		{
			VirtualWorldCycle();    // Cykl wirtualnego świata
			InvalidateRect(window_handle, NULL, FALSE);
		}
	}

	return (int)message.wParam;
}

/********************************************************************
FUNKCJA OKNA realizujaca przetwarzanie meldunków kierowanych do okna aplikacji*/
LRESULT CALLBACK WndProc(HWND window_handle, UINT message_code, WPARAM wParam, LPARAM lParam)
{

	switch (message_code)
	{
	case WM_CREATE:  //message wysyłany w momencie tworzenia okna
	{

		g_context = GetDC(window_handle);

		srand((unsigned)time(NULL));
		int result = GraphicsInitialization(g_context);
		if (result == 0)
		{
			printf("nie udalo sie otworzyc okna graficznego\n");
			//exit(1);
		}

		InteractionInitialization();

		SetTimer(window_handle, 1, 10, NULL);

		time_start = clock();      // by czas liczyć po utworzeniu okna i inicjalizacji 

		return 0;
	}


	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC context;
		context = BeginPaint(window_handle, &paint);

		DrawGLScene();
		SwapBuffers(context);

		EndPaint(window_handle, &paint);

		return 0;
	}

	case WM_TIMER:

		return 0;

	case WM_SIZE:
	{
		int cx = LOWORD(lParam);
		int cy = HIWORD(lParam);

		WindowResize(cx, cy);

		return 0;
	}

	case WM_DESTROY: //obowiązkowa obsługa meldunku o zamknięciu okna

		EndOfInteraction();
		EndOfGraphics();

		ReleaseDC(window_handle, g_context);
		KillTimer(window_handle, 1);

		//LPDWORD lpExitCode;
		DWORD ExitCode;
		GetExitCodeThread(threadReciv, &ExitCode);
		TerminateThread(threadReciv,ExitCode);
		//ExitThread(ExitCode);

		//Sleep(1000);

		other_vehicles.clear();
		

		PostQuitMessage(0);
		return 0;

	case WM_LBUTTONDOWN: //reakcja na lewy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
			my_vehicle->F = 180.0;        // siła pchająca do przodu
		break;
	}
	case WM_RBUTTONDOWN: //reakcja na prawy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
			my_vehicle->F = -110.0;        // siła pchająca do tylu
		break;
	}
	case WM_MBUTTONDOWN: //reakcja na środkowy przycisk myszki : uaktywnienie/dezaktywacja sterwania myszkowego
	{
		if_mouse_control = 1 - if_mouse_control;
		if (if_mouse_control) my_vehicle->if_keep_steer_wheel = true;
		else my_vehicle->if_keep_steer_wheel = false;

		mouse_cursor_x = LOWORD(lParam);
		mouse_cursor_y = HIWORD(lParam);
		break;
	}
	case WM_LBUTTONUP: //reakcja na puszczenie lewego przycisku myszki
	{
		if (if_mouse_control)
			my_vehicle->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_RBUTTONUP: //reakcja na puszczenie lewy przycisk myszki
	{
		if (if_mouse_control)
			my_vehicle->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
		{
			float wheel_angle = (float)(mouse_cursor_x - x) / 200;
			if (wheel_angle > my_vehicle->wheel_angle_max) wheel_angle = my_vehicle->wheel_angle_max;
			if (wheel_angle < -my_vehicle->wheel_angle_max) wheel_angle = -my_vehicle->wheel_angle_max;
			my_vehicle->state.wheel_angle = wheel_angle;
			//my_vehicle->wheel_turn_speed = (float)(mouse_cursor_x - x) / 20;
		}
		break;
	}
	case WM_KEYDOWN:
	{

		switch (LOWORD(wParam))
		{
		case VK_SHIFT:
		{
			if_SHIFT_pressed = 1;
			break;
		}
		case VK_SPACE:
		{
			my_vehicle->breaking_factor = 1.0;       // stopień hamowania (reszta zależy od siły docisku i wsp. tarcia)
			break;                       // 1.0 to maksymalny stopień (np. zablokowanie kół)
		}
		case VK_UP:
		{
			my_vehicle->F = 180.0;        // siła pchająca do przodu
			break;
		}
		case VK_DOWN:
		{
			my_vehicle->F = -110.0;
			break;
		}
		case VK_LEFT:
		{
			if (my_vehicle->wheel_turn_speed < 0){
				my_vehicle->wheel_turn_speed = 0;
				my_vehicle->if_keep_steer_wheel = true;
			}
			else{
				if (if_SHIFT_pressed) my_vehicle->wheel_turn_speed = 0.5;
				else my_vehicle->wheel_turn_speed = 0.25 / 8;
			}

			break;
		}
		case VK_RIGHT:
		{
			if (my_vehicle->wheel_turn_speed > 0){
				my_vehicle->wheel_turn_speed = 0;
				my_vehicle->if_keep_steer_wheel = true;
			}
			else{
				if (if_SHIFT_pressed) my_vehicle->wheel_turn_speed = -0.5;
				else my_vehicle->wheel_turn_speed = -0.25 / 8;
			}
			break;
		}
		case 'I':   // wypisywanie nr ID
		{
			if_ID_visible = 1 - if_ID_visible;
			break;
		}
		case 'W':   // cam_distance widoku
		{
			//cam_pos = cam_pos - cam_direct*0.3;
			if (view_parameters.cam_distance > 0.5) view_parameters.cam_distance /= 1.2;
			else view_parameters.cam_distance = 0;
			break;
		}
		case 'S':   // przybliżenie widoku
		{
			//cam_pos = cam_pos + cam_direct*0.3; 
			if (view_parameters.cam_distance > 0) view_parameters.cam_distance *= 1.2;
			else view_parameters.cam_distance = 0.5;
			break;
		}
		case 'Q':   // widok z góry
		{
			if (view_parameters.tracking) break;
			view_parameters.top_view = 1 - view_parameters.top_view;
			if (view_parameters.top_view)
			{
				view_parameters.cam_pos_1 = view_parameters.cam_pos; view_parameters.cam_direct_1 = view_parameters.cam_direct; view_parameters.cam_vertical_1 = view_parameters.cam_vertical;
				view_parameters.cam_distance_1 = view_parameters.cam_distance; view_parameters.cam_angle_1 = view_parameters.cam_angle;
				view_parameters.cam_pos = view_parameters.cam_pos_2; view_parameters.cam_direct = view_parameters.cam_direct_2; view_parameters.cam_vertical = view_parameters.cam_vertical_2;
				view_parameters.cam_distance = view_parameters.cam_distance_2; view_parameters.cam_angle = view_parameters.cam_angle_2;
			}
			else
			{
				view_parameters.cam_pos_2 = view_parameters.cam_pos; view_parameters.cam_direct_2 = view_parameters.cam_direct; view_parameters.cam_vertical_2 = view_parameters.cam_vertical;
				view_parameters.cam_distance_2 = view_parameters.cam_distance; view_parameters.cam_angle_2 = view_parameters.cam_angle;
				view_parameters.cam_pos = view_parameters.cam_pos_1; view_parameters.cam_direct = view_parameters.cam_direct_1; view_parameters.cam_vertical = view_parameters.cam_vertical_1;
				view_parameters.cam_distance = view_parameters.cam_distance_1; view_parameters.cam_angle = view_parameters.cam_angle_1;
			}
			break;
		}
		case 'E':   // obrót kamery ku górze (względem lokalnej osi z)
		{
			view_parameters.cam_angle += PI * 5 / 180;
			break;
		}
		case 'D':   // obrót kamery ku dołowi (względem lokalnej osi z)
		{
			view_parameters.cam_angle -= PI * 5 / 180;
			break;
		}
		case 'A':   // włączanie, wyłączanie trybu śledzenia obiektu
		{
			view_parameters.tracking = 1 - view_parameters.tracking;
			if (view_parameters.tracking)
			{
				view_parameters.cam_distance = view_parameters.cam_distance_3; view_parameters.cam_angle = view_parameters.cam_angle_3;
			}
			else
			{
				view_parameters.cam_distance_3 = view_parameters.cam_distance; view_parameters.cam_angle_3 = view_parameters.cam_angle;
				view_parameters.top_view = 0;
				view_parameters.cam_pos = view_parameters.cam_pos_1; view_parameters.cam_direct = view_parameters.cam_direct_1; view_parameters.cam_vertical = view_parameters.cam_vertical_1;
				view_parameters.cam_distance = view_parameters.cam_distance_1; view_parameters.cam_angle = view_parameters.cam_angle_1;
			}
			break;
		}
		case 'Z':   // zoom - zmniejszenie kąta widzenia
		{
			view_parameters.zoom /= 1.1;
			RECT rc;
			GetClientRect(window_handle, &rc);
			WindowResize(rc.right - rc.left, rc.bottom - rc.top);
			break;
		}
		case 'X':   // zoom - zwiększenie kąta widzenia
		{
			view_parameters.zoom *= 1.1;
			RECT rc;
			GetClientRect(window_handle, &rc);
			WindowResize(rc.right - rc.left, rc.bottom - rc.top);
			break;
		}
		case VK_F1:  // wywolanie systemu pomocy
		{
			char lan[1024], lan_bie[1024];
			//GetSystemDirectory(lan_sys,1024);
			GetCurrentDirectory(1024, lan_bie);
			strcpy(lan, "C:\\Program Files\\Internet Explorer\\iexplore ");
			strcat(lan, lan_bie);
			strcat(lan, "\\pomoc.htm");
			int wyni = WinExec(lan, SW_NORMAL);
			if (wyni < 32)  // proba uruchominia pomocy nie powiodla sie
			{
				strcpy(lan, "C:\\Program Files\\Mozilla Firefox\\firefox ");
				strcat(lan, lan_bie);
				strcat(lan, "\\pomoc.htm");
				wyni = WinExec(lan, SW_NORMAL);
				if (wyni < 32)
				{
					char lan_win[1024];
					GetWindowsDirectory(lan_win, 1024);
					strcat(lan_win, "\\notepad pomoc.txt ");
					wyni = WinExec(lan_win, SW_NORMAL);
				}
			}
			break;
		}
		case VK_ESCAPE:
		{
			SendMessage(window_handle, WM_DESTROY, 0, 0);
			break;
		}
		case 'C':         // przełącznie widoku z kokpitu pojazdu użytkownka i jego cienia sieciowego
		{
			view_parameters.network_shadow_view = 1 - view_parameters.network_shadow_view;
			break;
		}
		} // switch po klawiszach

		break;
	}
	case WM_KEYUP:
	{
		switch (LOWORD(wParam))
		{
		case VK_SHIFT:
		{
			if_SHIFT_pressed = 0;
			break;
		}
		case VK_SPACE:
		{
			my_vehicle->breaking_factor = 0.0;
			break;
		}
		case VK_UP:
		{
			my_vehicle->F = 0.0;
			break;
		}
		case VK_DOWN:
		{
			my_vehicle->F = 0.0;
			break;
		}
		case VK_LEFT:
		{
			my_vehicle->Fb = 0.00;
			//my_vehicle->state.wheel_angle = 0;
			if (my_vehicle->if_keep_steer_wheel) my_vehicle->wheel_turn_speed = -0.25/8;
			else my_vehicle->wheel_turn_speed = 0; 
			my_vehicle->if_keep_steer_wheel = false;
			break;
		}
		case VK_RIGHT:
		{
			my_vehicle->Fb = 0.00;
			//my_vehicle->state.wheel_angle = 0;
			if (my_vehicle->if_keep_steer_wheel) my_vehicle->wheel_turn_speed = 0.25 / 8;
			else my_vehicle->wheel_turn_speed = 0;
			my_vehicle->if_keep_steer_wheel = false;
			break;
		}

		}

		break;
	}

	default: //statedardowa obsługa pozostałych meldunków
		return DefWindowProc(window_handle, message_code, wParam, lParam);
	}


}

