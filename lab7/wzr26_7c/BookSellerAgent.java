/*  Klasa agenta sprzedawcy książek.
 *  Sprzedawca dysponuje katalogiem książek oraz dwoma klasami zachowań:
 *  - OfferRequestsServer - obsługa odpowiedzi na oferty klientów
 *  - PurchaseOrdersServer - obsługa zamówienia klienta
 *
 *  parametry linii uruchomienia:
 *  -agents seller1:BookSellerAgent();seller2:BookSellerAgent();buyer1:BookBuyerAgent(Zamek) -gui
*/
import jade.core.Agent;
import jade.core.behaviours.*;
import jade.lang.acl.*;
import java.util.*;
import java.lang.*;


public class BookSellerAgent extends Agent
{
  // Katalog lektur na sprzedaż:
  private Hashtable catalogue;
  private HashMap negotiationPrice;
  private HashMap negotiationCuts;
  private static final int PRICE_CUT = 4;
  private static final int MAX_CUTS = 6;

  // Inicjalizacja klasy agenta:
  protected void setup()
  {
    // Tworzenie katalogu lektur jako tablicy rozproszonej
    catalogue = new Hashtable();
    negotiationPrice = new HashMap();
    negotiationCuts = new HashMap();

    Random randomGenerator = new Random();    // generator liczb losowych

    catalogue.put("Zamek", 50+randomGenerator.nextInt(500));       // nazwa lektury jako klucz, cena jako wartość
    catalogue.put("Opowiadania", 110+randomGenerator.nextInt(200));
    catalogue.put("Ameryka", 300+randomGenerator.nextInt(70));
    catalogue.put("Proces", 250+randomGenerator.nextInt(250));
    catalogue.put("WZR_dla_opornych", 30+randomGenerator.nextInt(20));

    doWait(2024);                     // czekaj 2 sekundy

    System.out.println("Witam! Agent-sprzedawca (wersja c <2025/26>) "+getAID().getName()+" jest gotów do działania!");

    // Dodanie zachowania obsługującego odpowiedzi na oferty klientów (kupujących książki):
    addBehaviour(new OfferRequestsServer());

    // Dodanie zachowania obsługującego zamówienie klienta:
    addBehaviour(new PurchaseOrdersServer());
  }

  // Metoda realizująca zakończenie pracy agenta:
  protected void takeDown()
  {
    System.out.println("Agent-sprzedawca (wersja c <2025/26>) "+getAID().getName()+" wychodzi. Do widzenia!");
  }


  /**
    Inner class OfferRequestsServer.
    This is the behaviour used by Book-seller agents to serve incoming requests
    for offer from buyer agents.
    If the requested book is in the local catalogue the seller agent replies
    with a PROPOSE message specifying the price. Otherwise a REFUSE message is sent back.
    */
    class OfferRequestsServer extends CyclicBehaviour
    {
      public void action()
      {
        // Tworzenie szablonu wiadomości (wstępne określenie tego, co powinna zawierać wiadomość)
        MessageTemplate mt = MessageTemplate.or(
          MessageTemplate.MatchPerformative(ACLMessage.CFP),
          MessageTemplate.MatchPerformative(ACLMessage.PROPOSE)
        );
        // Próba odbioru wiadomości zgodnej z szablonem:
        ACLMessage msg = myAgent.receive(mt);
        if (msg != null) {  // jeśli nadeszła wiadomość zgodna z ustalonym wcześniej szablonem
          String content = msg.getContent();
          String title = content;
          int separatorIndex = content.indexOf("|");
          if (separatorIndex >= 0) {
            title = content.substring(0, separatorIndex);
          }
          String key = msg.getSender().getLocalName()+":"+title;

          System.out.println("Agent-sprzedawca  "+getAID().getName()+" otrzymał komunikat: "+
                   title);
          ACLMessage reply = msg.createReply();               // tworzenie wiadomości - odpowiedzi
          Integer price = (Integer) catalogue.get(title);     // ustalenie ceny dla podanego tytułu
          if (price != null) {                                // jeśli taki tytuł jest dostępny
            int currentPrice = price.intValue();
            if (msg.getPerformative() == ACLMessage.CFP) {
              negotiationPrice.put(key, Integer.valueOf(currentPrice));
              negotiationCuts.put(key, Integer.valueOf(0));
            } else {
              Integer storedPrice = (Integer) negotiationPrice.get(key);
              Integer storedCuts = (Integer) negotiationCuts.get(key);
              if (storedPrice != null && storedCuts != null) {
                currentPrice = storedPrice.intValue();
                int cuts = storedCuts.intValue();
                if (cuts < MAX_CUTS) {
                  currentPrice = currentPrice - PRICE_CUT;
                  cuts++;
                  negotiationPrice.put(key, Integer.valueOf(currentPrice));
                  negotiationCuts.put(key, Integer.valueOf(cuts));
                }
              }
            }

            reply.setPerformative(ACLMessage.PROPOSE);            // ustalenie typu wiadomości (propozycja)
            reply.setContent(String.valueOf(currentPrice));       // umieszczenie ceny w polu zawartości (content)
            System.out.println("Agent-sprzedawca "+getAID().getName()+" odpowiada: "+
                   currentPrice);
          }
          else {                                              // jeśli tytuł niedostępny
            // The requested book is NOT available for sale.
            reply.setPerformative(ACLMessage.REFUSE);         // ustalenie typu wiadomości (odmowa)
            reply.setContent("tytuł niestety niedostępny");                  // treść wiadomości
          }
          myAgent.send(reply);                                // wysłanie odpowiedzi
        }
        else                         // jeśli wiadomość nie nadeszła, lub była niezgodna z szablonem
        {
          block();                   // blokada metody action() dopóki nie pojawi się nowa wiadomość
        }
      }
    } // Koniec klasy wewnętrznej będącej rozszerzeniem klasy CyclicBehaviour


    class PurchaseOrdersServer extends CyclicBehaviour
    {
      public void action()
      {
        MessageTemplate mt = MessageTemplate.MatchPerformative(ACLMessage.ACCEPT_PROPOSAL);
        ACLMessage msg = myAgent.receive(mt);

        if ((msg != null)&&(msg.getPerformative() == ACLMessage.ACCEPT_PROPOSAL))
        {
          // Message received. Process it          
          ACLMessage reply = msg.createReply();
          String title = msg.getContent();
          reply.setPerformative(ACLMessage.INFORM);
          System.out.println("Agent sprzedający (wersja c <2025/26>) "+getAID().getName()+" sprzedał książkę: "+title);
          myAgent.send(reply);
        }
        else
        {
          block();
        }
      }
    } // Koniec klasy wewnętrznej będącej rozszerzeniem klasy CyclicBehaviour
} // Koniec klasy będącej rozszerzeniem klasy Agent