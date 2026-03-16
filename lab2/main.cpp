/****************************************************
	Virtual Collaborative Teams - The base program 
    The main module
	****************************************************/

#include <windows.h>
#include <math.h>
#include <time.h>
#include <float.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include <iterator> 
#include <map>
#include <vector>

#include "objects.h"
#include "graphics.h"
#include "net.h"
using namespace std;

#define SERVER_IP "172.20.14.180"
bool is_server = false;
unicast_net* uni_reciv;
unicast_net* uni_send;

map<unsigned long, clock_t> clients; // dla serwera: mapa IP klientow -> czas ostatniej aktywnosci

FILE *f = fopen("vct_log.txt", "w"); // plik do zapisu informacji testowych

bool client_spawn_position_received = false;  // Flag to track if spawn position is received
Vector3 client_spawn_position;                // Variable to store the spawn position received from the server

MovableObject *my_car;               // obiekt przypisany do tej aplikacji
Environment env;

map<int, MovableObject*> other_cars;

float avg_cycle_time;                // sredni czas pomiedzy dwoma kolejnymi cyklami symulacji i wyswietlania
long time_of_cycle, number_of_cyc;   // zmienne pomocnicze potrzebne do obliczania avg_cycle_time
long time_start = clock();

HANDLE threadReciv;                  // uchwyt wątku odbioru komunikatów
HWND main_window;                    // uchwyt do głównego okna programu 
CRITICAL_SECTION m_cs;               // do synchronizacji wątków

bool if_SHIFT_pressed = false;
bool if_ID_visible = true;           // czy rysowac nr ID przy każdym obiekcie
bool if_mouse_control = false;       // sterowanie za pomocą klawisza myszki
int mouse_cursor_x = 0, mouse_cursor_y = 0;     // położenie kursora myszy

// false until my_car's spawn position has been set from an existing car
bool my_car_spawned = false;

// clock() tick when the first foreign car was first seen (0 = not yet seen)
long spawn_settle_start = 0;

// how long to wait after seeing the first car before picking a spawn position [ms]
const long spawn_settle_ms = 500;

// minimum spawn distance from any other car [m]; map is ~8084x8084
const float spawn_distance = 10.0f;

// network frame types
const int FRAME_TYPE_STATE = 1;
const int FRAME_TYPE_SPAWN = 2;
const int FRAME_TYPE_DELETE = 3;

// server-side inactivity timeout
const clock_t client_timeout_ticks = 5 * CLOCKS_PER_SEC;

extern ViewParams viewpar;           // ustawienia widoku zdefiniowane w grafice

long duration_of_day = 800;         // czas trwania dnia w [s]

struct Frame                                      // główna struktura służąca do przesyłania informacji
{	
	int iID;                                      // identyfikator obiektu, którego 
	int type;                                     // typ ramki: informacja o stateie, informacja o zamknięciu, komunikat tekstowy, ... 
	ObjectState state;                            // położenie, prędkość: środka masy + kątowe, ...

	long sending_time;                            // tzw. znacznik czasu potrzebny np. do obliczenia opóźnienia
	int iID_receiver;                             // nr ID odbiorcy wiadomości, jeśli skierowana jest tylko do niego
};

Vector3 CalculateBestSpawnPosition(const std::map<unsigned long, MovableObject*>& other_cars, const Vector3& fallback_pos, unsigned long ignored_ip);

DWORD WINAPI ServerThreadFun(void* ptr)
{
	unicast_net* s_reciv = new unicast_net(1001);
	unicast_net* s_send = new unicast_net(1002);
	Frame frame;
	unsigned long senderIP;
	map<unsigned long, int> client_ids;

	// Map to store positions of other cars
	std::map<unsigned long, MovableObject*> other_cars;

	while (1)
	{
		int bytes = s_reciv->reciv((char*)&frame, &senderIP, sizeof(Frame));
		if (bytes == sizeof(Frame))
		{
			if (frame.type != FRAME_TYPE_STATE)
			{
				continue;
			}

			clock_t now = clock();
			bool is_new_client = (clients.find(senderIP) == clients.end());

			// Register or update the client
			clients[senderIP] = now;
			client_ids[senderIP] = frame.iID;

			// Update the position of the sender's car
			MovableObject* sender_car = NULL;
			auto sender_it = other_cars.find(senderIP);
			if (sender_it == other_cars.end())
			{
				sender_car = new MovableObject();
				sender_car->iID = frame.iID;
				other_cars[senderIP] = sender_car;
			}
			else
			{
				sender_car = sender_it->second;
			}
			sender_car->ChangeState(frame.state);  // Update the car's state (position, etc.)

			// For a new client, server assigns an authoritative spawn position once.
			if (is_new_client)
			{
				Vector3 spawn_pos = CalculateBestSpawnPosition(other_cars, frame.state.vPos, senderIP);
				ObjectState spawned_state = frame.state;
				spawned_state.vPos = spawn_pos;
				sender_car->ChangeState(spawned_state);

				Frame spawn_frame = frame;
				spawn_frame.type = FRAME_TYPE_SPAWN;
				spawn_frame.state = spawned_state;
				s_send->send((char*)&spawn_frame, senderIP, sizeof(Frame));

				// Broadcast state with authoritative spawn position.
				frame.state = spawned_state;
			}

			frame.type = FRAME_TYPE_STATE;

			// Broadcast the updated state to all clients.
			for (auto it = clients.begin(); it != clients.end(); ++it)
			{
				s_send->send((char*)&frame, it->first, sizeof(Frame));
			}

			// Detect and remove inactive clients.
			vector<unsigned long> timed_out_ips;
			for (auto it = clients.begin(); it != clients.end(); ++it)
			{
				if (now - it->second > client_timeout_ticks)
				{
					timed_out_ips.push_back(it->first);
				}
			}

			for (size_t i = 0; i < timed_out_ips.size(); ++i)
			{
				unsigned long dead_ip = timed_out_ips[i];
				int dead_id = 0;
				auto id_it = client_ids.find(dead_ip);
				if (id_it != client_ids.end())
				{
					dead_id = id_it->second;
				}

				printf("Removed inactive client: %lu (car id: %d)\n", dead_ip, dead_id);

				auto car_it = other_cars.find(dead_ip);
				if (car_it != other_cars.end())
				{
					delete car_it->second;
					other_cars.erase(car_it);
				}

				clients.erase(dead_ip);
				client_ids.erase(dead_ip);

				if (dead_id != 0)
				{
					Frame delete_frame = {};
					delete_frame.type = FRAME_TYPE_DELETE;
					delete_frame.iID = dead_id;
					for (auto it = clients.begin(); it != clients.end(); ++it)
					{
						s_send->send((char*)&delete_frame, it->first, sizeof(Frame));
					}
				}
			}
		}
	}
	return 1;
}

// Function to calculate the best spawn position for a new car
Vector3 CalculateBestSpawnPosition(const std::map<unsigned long, MovableObject*>& other_cars, const Vector3& fallback_pos, unsigned long ignored_ip)
{
	std::vector<Vector3> positions;
	for (auto& kv : other_cars)
	{
		if (kv.first == ignored_ip) continue;
		if (kv.second) positions.push_back(kv.second->State().vPos);
	}

	if (positions.empty())
	{
		return fallback_pos;
	}

	// Take the first car position as a reference
	Vector3 ref = positions[0];

	// Constants
	const int max_tries = 36;

	// Try generating positions
	for (int i = 0; i < max_tries; i++)
	{
		// Random angle generation
		float angle = (float)i * (2.0f * (float)M_PI / max_tries)
			+ ((float)(rand() % 100) / 100.0f) * 0.1f;
		Vector3 candidate;
		candidate.x = ref.x + spawn_distance * cosf(angle);
		candidate.z = ref.z + spawn_distance * sinf(angle);
		candidate.y = ref.y; // Assume height is managed by simulation

		// Check the distances
		bool valid = true;
		float closest_dist = FLT_MAX;
		for (auto& p : positions)
		{
			float dx = candidate.x - p.x;
			float dz = candidate.z - p.z;
			float dist = sqrtf(dx * dx + dz * dz);

			// If any car is too close, mark as invalid
			if (dist < spawn_distance)
			{
				valid = false;
				break;
			}

			// Track the closest car distance
			if (dist < closest_dist)
			{
				closest_dist = dist;
			}
		}

		// If the closest car is exactly spawn_distance, and all cars are far enough, accept the candidate
		if (valid && closest_dist >= spawn_distance)
		{
			return candidate;
		}
	}

	// Return a fallback spawn if no valid position found after max tries (could be ref or another fallback)
	return fallback_pos;
}
//******************************************
// Funkcja obsługi wątku odbioru komunikatów 
// UWAGA!  Odbierane są też komunikaty z własnej aplikacji by porównać obraz ekstrapolowany do rzeczywistego.
DWORD WINAPI ReceiveThreadFun(void* ptr)
{
	unicast_net* pmt_net = (unicast_net*)ptr;  // Pointer to unicast_net
	Frame frame;
	unsigned long senderIP;

	while (1)
	{
		int frame_size = pmt_net->reciv((char*)&frame, &senderIP, sizeof(Frame));   // Wait for frame
		if (frame_size != sizeof(Frame))
		{
			continue;
		}
		ObjectState state = frame.state;

		EnterCriticalSection(&m_cs);  // Enter critical section to modify shared resources

		if (frame.type == FRAME_TYPE_DELETE)
		{
			if (frame.iID != my_car->iID)
			{
				auto it = other_cars.find(frame.iID);
				if (it != other_cars.end())
				{
					delete it->second;
					other_cars.erase(it);
				}
			}
			LeaveCriticalSection(&m_cs);
			continue;
		}

		if (frame.type == FRAME_TYPE_SPAWN && frame.iID == my_car->iID && !client_spawn_position_received)
		{
			client_spawn_position = frame.state.vPos;
			client_spawn_position_received = true;
			LeaveCriticalSection(&m_cs);
			continue;
		}

		if (frame.type != FRAME_TYPE_STATE)
		{
			LeaveCriticalSection(&m_cs);
			continue;
		}

		if (frame.iID != my_car->iID)  // If this is not the client's own object
		{
			if ((other_cars.size() == 0) || (other_cars[frame.iID] == NULL))
			{
				MovableObject* ob = new MovableObject();
				ob->iID = frame.iID;
				other_cars[frame.iID] = ob;
			}
			other_cars[frame.iID]->ChangeState(state);   // Update the state of foreign objects
		}
		LeaveCriticalSection(&m_cs);  // Exit critical section
	}
	return 1;
}

// *****************************************************************
// ****    Wszystko co trzeba zrobić podczas uruchamiania aplikacji
// ****    poza grafiką   
void InteractionInitialisation()
{
	DWORD dwThreadId;

	is_server = (MessageBox(NULL, "Czy ta aplikacja to serwer?", "Wybór roli", MB_YESNO) == IDYES);

	if (is_server) {
		CreateThread(NULL, 0, ServerThreadFun, NULL, 0, &dwThreadId);
	}

	my_car = new MovableObject();    // tworzenie wlasnego obiektu

	time_of_cycle = clock();             // pomiar aktualnego czasu

	// obiekty sieciowe typu unicast
	uni_reciv = new unicast_net(1002);      // klient odbiera na 1002
	uni_send = new unicast_net(1001);       // klient wysyła do serwera (port 1001)

	// uruchomienie wątku obsługującego odbiór komunikatów:
	threadReciv = CreateThread(
		NULL,                        // no security attributes
		0,                           // use default stack size
		ReceiveThreadFun,                // thread function
		(void*)uni_reciv,               // argument to thread function
		NULL,                        // use default creation flags
		&dwThreadId);                // returns the thread identifier
	SetThreadPriority(threadReciv, THREAD_PRIORITY_HIGHEST);

	printf("start interakcji\n");
}


// *****************************************************************
// ****    Wszystko co trzeba zrobić w każdym cyklu działania 
// ****    aplikacji poza grafiką 
void VirtualWorldCycle()
{
	number_of_cyc++;

	// --- Handle the spawn positioning now decided by the server ---
	if (!my_car_spawned)
	{
		EnterCriticalSection(&m_cs);
		if (client_spawn_position_received)
		{
			my_car->state.vPos = client_spawn_position;
			my_car_spawned = true;
		}
		LeaveCriticalSection(&m_cs);
	}

	if (number_of_cyc % 50 == 0)          // jeśli licznik cykli przekroczył pewną wartość, to
	{                              // należy na nowo obliczyć średni czas cyklu avg_cycle_time
		char text[256];
		long prev_time = time_of_cycle;
		time_of_cycle = clock();
		float fFps = (50 * CLOCKS_PER_SEC) / (float)(time_of_cycle - prev_time);
		if (fFps != 0) avg_cycle_time = 1.0 / fFps; else avg_cycle_time = 1;

		sprintf(text, "WZR-lab 2025/26 (lato) temat 1, wersja g (car_id:%d, %0.0f fps  %0.2fms, x:%0.2f, y:%0.2f, z:%0.2f) ", my_car->iID, fFps, 1000.0 / fFps, my_car->state.vPos.x, my_car->state.vPos.y, my_car->state.vPos.z);

		SetWindowText(main_window, text); // wyświetlenie aktualnej ilości klatek/s w pasku okna			
	}

	my_car->Simulation(avg_cycle_time);                    // symulacja własnego obiektu

	Frame frame;
	frame.state = my_car->State();               // state własnego obiektu 
	frame.iID = my_car->iID;
	frame.type = FRAME_TYPE_STATE;

	uni_send->send((char*)&frame, (char*)SERVER_IP, sizeof(Frame));  // wysłanie komunikatu do serwera
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
	
	//Initilze the critical section
	InitializeCriticalSection(&m_cs);

	MSG message;		  //innymi slowy "komunikat"
	WNDCLASS main_class; //klasa głównego okna aplikacji

	static char class_name[] = "Klasa_Podstawowa";

	//Definiujemy klase głównego okna aplikacji
	//Okreslamy tu wlasciwosci okna, szczegóły wyglądu oraz
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

	//teraz rejestrujemy klasę okna głównego
	RegisterClass(&main_class);

	main_window = CreateWindow(class_name, "WZR-lab 2025/26 (lato) temat 1 - wersja g", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		100, 50, 950, 650, NULL, NULL, hInstance, NULL);

	

	ShowWindow(main_window, nCmdShow);

	//odswiezamy zawartosc okna
	UpdateWindow(main_window);

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
			InvalidateRect(main_window, NULL, FALSE);
		}
	}

	return (int)message.wParam;
}

/********************************************************************
FUNKCJA OKNA realizujaca przetwarzanie meldunków kierowanych do okna aplikacji*/
LRESULT CALLBACK WndProc(HWND main_window, UINT message_code, WPARAM wParam, LPARAM lParam)
{

	switch (message_code)
	{
	case WM_CREATE:  //message wysyłany w momencie tworzenia okna
	{

		g_context = GetDC(main_window);

		srand((unsigned)time(NULL));
		int result = GraphicsInitialisation(g_context);
		if (result == 0)
		{
			printf("nie udalo sie otworzyc okna graficznego\n");
			//exit(1);
		}

		InteractionInitialisation();

		SetTimer(main_window, 1, 10, NULL);

		return 0;
	}


	case WM_PAINT:
	{
		PAINTSTRUCT paint;
		HDC context;
		context = BeginPaint(main_window, &paint);

		DrawScene();
		SwapBuffers(context);

		EndPaint(main_window, &paint);

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

		ReleaseDC(main_window, g_context);
		KillTimer(main_window, 1);

		//LPDWORD lpExitCode;
		DWORD ExitCode;
		GetExitCodeThread(threadReciv, &ExitCode);
		TerminateThread(threadReciv,ExitCode);
		//ExitThread(ExitCode);

		//Sleep(1000);

		other_cars.clear();
		

		PostQuitMessage(0);
		return 0;

	case WM_LBUTTONDOWN: //reakcja na lewy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
			my_car->F = 30.0;        // siła pchająca do przodu
		break;
	}
	case WM_RBUTTONDOWN: //reakcja na prawy przycisk myszki
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
			my_car->F = -5.0;        // siła pchająca do tylu
		break;
	}
	case WM_MBUTTONDOWN: //reakcja na środkowy przycisk myszki : uaktywnienie/dezaktywacja sterwania myszkowego
	{
		if_mouse_control = 1 - if_mouse_control;
		if (if_mouse_control) my_car->if_keep_steer_wheel = true;
		else my_car->if_keep_steer_wheel = false;

		mouse_cursor_x = LOWORD(lParam);
		mouse_cursor_y = HIWORD(lParam);
		break;
	}
	case WM_LBUTTONUP: //reakcja na puszczenie lewego przycisku myszki
	{
		if (if_mouse_control)
			my_car->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_RBUTTONUP: //reakcja na puszczenie lewy przycisk myszki
	{
		if (if_mouse_control)
			my_car->F = 0.0;        // siła pchająca do przodu
		break;
	}
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (if_mouse_control)
		{
			float wheel_angle = (float)(mouse_cursor_x - x) / 20;
			if (wheel_angle > 60) wheel_angle = 60;
			if (wheel_angle < -60) wheel_angle = -60;
			my_car->state.steering_angle = PI*wheel_angle / 180;
			//my_car->steer_wheel_speed = (float)(mouse_cursor_x - x) / 20;
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
			my_car->breaking_factor = 1.0;       // stopień hamowania (reszta zależy od siły docisku i wsp. tarcia)
			break;                       // 1.0 to maksymalny stopień (np. zablokowanie kół)
		}
		case VK_UP:
		{
			my_car->F = 200.0;        // siła pchająca do przodu
			break;
		}
		case VK_DOWN:
		{
			my_car->F = -140.0;
			break;
		}
		case VK_LEFT:
		{
			if (my_car->steer_wheel_speed < 0){
				my_car->steer_wheel_speed = 0;
				my_car->if_keep_steer_wheel = true;
			}
			else{
				if (if_SHIFT_pressed) my_car->steer_wheel_speed = 1.0;
				else my_car->steer_wheel_speed = 0.25 / 4;
			}

			break;
		}
		case VK_RIGHT:
		{
			if (my_car->steer_wheel_speed > 0){
				my_car->steer_wheel_speed = 0;
				my_car->if_keep_steer_wheel = true;
			}
			else{
				if (if_SHIFT_pressed) my_car->steer_wheel_speed = -1.0;
				else my_car->steer_wheel_speed = -0.25 / 4;
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
			if (viewpar.cam_distance > 0.5) viewpar.cam_distance /= 1.2;
			else viewpar.cam_distance = 0;
			break;
		}
		case 'S':   // przybliżenie widoku
		{
			//cam_pos = cam_pos + cam_direct*0.3; 
			if (viewpar.cam_distance > 0) viewpar.cam_distance *= 1.2;
			else viewpar.cam_distance = 0.5;
			break;
		}
		case 'Q':   // widok z góry
		{
			if (viewpar.tracking) break;
			viewpar.top_view = 1 - viewpar.top_view;
			if (viewpar.top_view)
			{
				viewpar.cam_pos_1 = viewpar.cam_pos; viewpar.cam_direct_1 = viewpar.cam_direct; viewpar.cam_vertical_1 = viewpar.cam_vertical;
				viewpar.cam_distance_1 = viewpar.cam_distance; viewpar.cam_angle_1 = viewpar.cam_angle;
				viewpar.cam_pos = viewpar.cam_pos_2; viewpar.cam_direct = viewpar.cam_direct_2; viewpar.cam_vertical = viewpar.cam_vertical_2;
				viewpar.cam_distance = viewpar.cam_distance_2; viewpar.cam_angle = viewpar.cam_angle_2;
			}
			else
			{
				viewpar.cam_pos_2 = viewpar.cam_pos; viewpar.cam_direct_2 = viewpar.cam_direct; viewpar.cam_vertical_2 = viewpar.cam_vertical;
				viewpar.cam_distance_2 = viewpar.cam_distance; viewpar.cam_angle_2 = viewpar.cam_angle;
				viewpar.cam_pos = viewpar.cam_pos_1; viewpar.cam_direct = viewpar.cam_direct_1; viewpar.cam_vertical = viewpar.cam_vertical_1;
				viewpar.cam_distance = viewpar.cam_distance_1; viewpar.cam_angle = viewpar.cam_angle_1;
			}
			break;
		}
		case 'E':   // obrót kamery ku górze (względem lokalnej osi z)
		{
			viewpar.cam_angle += PI * 5 / 180;
			break;
		}
		case 'D':   // obrót kamery ku dołowi (względem lokalnej osi z)
		{
			viewpar.cam_angle -= PI * 5 / 180;
			break;
		}
		case 'A':   // włączanie, wyłączanie trybu śledzenia obiektu
		{
			viewpar.tracking = 1 - viewpar.tracking;
			if (viewpar.tracking)
			{
				viewpar.cam_distance = viewpar.cam_distance_3; viewpar.cam_angle = viewpar.cam_angle_3;
			}
			else
			{
				viewpar.cam_distance_3 = viewpar.cam_distance; viewpar.cam_angle_3 = viewpar.cam_angle;
				viewpar.top_view = 0;
				viewpar.cam_pos = viewpar.cam_pos_1; viewpar.cam_direct = viewpar.cam_direct_1; viewpar.cam_vertical = viewpar.cam_vertical_1;
				viewpar.cam_distance = viewpar.cam_distance_1; viewpar.cam_angle = viewpar.cam_angle_1;
			}
			break;
		}
		case 'Z':   // zoom - zmniejszenie kąta widzenia
		{
			viewpar.zoom /= 1.1;
			RECT rc;
			GetClientRect(main_window, &rc);
			WindowResize(rc.right - rc.left, rc.bottom - rc.top);
			break;
		}
		case 'X':   // zoom - zwiększenie kąta widzenia
		{
			viewpar.zoom *= 1.1;
			RECT rc;
			GetClientRect(main_window, &rc);
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
			SendMessage(main_window, WM_DESTROY, 0, 0);
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
			my_car->breaking_factor = 0.0;
			break;
		}
		case VK_UP:
		{
			my_car->F = 0.0;
			break;
		}
		case VK_DOWN:
		{
			my_car->F = 0.0;
			break;
		}
		case VK_LEFT:
		{
			my_car->Fb = 0.00;
			if (my_car->if_keep_steer_wheel) my_car->steer_wheel_speed = -0.25/4;
			else my_car->steer_wheel_speed = 0; 
			my_car->if_keep_steer_wheel = false;
			break;
		}
		case VK_RIGHT:
		{
			my_car->Fb = 0.00;
			if (my_car->if_keep_steer_wheel) my_car->steer_wheel_speed = 0.25 / 4;
			else my_car->steer_wheel_speed = 0;
			my_car->if_keep_steer_wheel = false;
			break;
		}

		}

		break;
	}

	default: //statedardowa obsługa pozostałych meldunków
		return DefWindowProc(main_window, message_code, wParam, lParam);
	}


}

