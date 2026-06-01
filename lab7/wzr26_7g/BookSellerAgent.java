// Przykładowa lista parametrów uruchomienia:
// -agents seller1:BookSellerAgent();seller2:BookSellerAgent();buyer1:BookBuyerAgent(Zamek) -gui
import jade.core.Agent;
import jade.core.behaviours.*;
import jade.lang.acl.*;
import java.util.*;
import java.lang.*;


public class BookSellerAgent extends Agent
{
  // Katalog książek na sprzedaż:
  private Hashtable catalogue;
  private static final int PRICE_DECREMENT = 2;
  private static final int MAX_DECREMENTS = 5;
  private Map currentPrices = new HashMap();
  private Map decrementCounts = new HashMap();

  // Inicjalizacja klasy agenta:
  protected void setup()
  {
    // Tworzenie katalogu książek jako tablicy rozproszonej
    catalogue = new Hashtable();

    Random randomGenerator = new Random();    // generator liczb losowych

    catalogue.put("Zamek", 150+randomGenerator.nextInt(200));       // nazwa książki jako klucz, cena jako wartość
    catalogue.put("Proces", 200+randomGenerator.nextInt(170));
    catalogue.put("Opowiadania", 110+randomGenerator.nextInt(50));
    catalogue.put("Wirtualne życie", 120+randomGenerator.nextInt(140));
    catalogue.put("Agenty autonomiczne", 270+randomGenerator.nextInt(80));

    doWait(2000+25);                     // czekaj 2 sekundy

    System.out.println("Witam! Agent-sprzedawca (wersja g lato,2025/26) "+getAID().getName()+" jest gotów do sprzedawania");

    // Dodanie zachowania obsługującego odpowiedzi na oferty klientów (kupujących książki):
    addBehaviour(new OfferRequestsServer());

    // Dodanie zachowania obsługującego zamówienie klienta:
    addBehaviour(new PurchaseOrdersServer());
  }

  // Metoda realizująca zakończenie pracy agenta:
  protected void takeDown()
  {
    System.out.println("Agent-sprzedawca (wersja g lato,2025/26) "+getAID().getName()+" sobie poszedł.");
  }


  /**
    Inner class OfferRequestsServer.
    This is the behaviour used by Book-seller agents to serve incoming requests
    for offer from buyer agents.
    If the requested book is in the local catalogue the seller agent replies
    with a PROPOSE message specifying the price. Otherwise a REFUSE message is
    sent back.
    */
    class OfferRequestsServer extends CyclicBehaviour
    {
      public void action()
      {
        // Tworzenie szablonu wiadomości (wstępne określenie tego, co powinna zawierać wiadomość)
        MessageTemplate mt = MessageTemplate.or(
          MessageTemplate.MatchPerformative(ACLMessage.CFP),
          MessageTemplate.MatchPerformative(ACLMessage.PROPOSE));
        // Próba odbioru wiadomości zgodnej z szablonem:
        ACLMessage msg = myAgent.receive(mt);
        if (msg != null) {  // jeśli nadeszła wiadomość zgodna z ustalonym wcześniej szablonem
          String senderKey = msg.getSender().getName();
          ACLMessage reply = msg.createReply();               // tworzenie wiadomości - odpowiedzi
          if (msg.getPerformative() == ACLMessage.CFP)
          {
            String title = msg.getContent();  // odczytanie tytułu zamawianej książki

            System.out.println("Agent-sprzedawca "+getAID().getName()+" otrzymał wiadomość: "+
                     title);
            Integer price = (Integer) catalogue.get(title);     // ustalenie ceny dla podanego tytułu
            if (price != null) {                                // jeśli taki tytuł jest dostępny
              reply.setPerformative(ACLMessage.PROPOSE);            // ustalenie typu wiadomości (propozycja)
              reply.setContent(String.valueOf(price.intValue()));   // umieszczenie ceny w polu zawartości (content)
              currentPrices.put(senderKey, price);
              decrementCounts.put(senderKey, new Integer(0));
              System.out.println("Agent-sprzedawca "+getAID().getName()+" odpowiada: "+
                     price.intValue());
            }
            else {                                              // jeśli tytuł niedostępny
              // The requested book is NOT available for sale.
              reply.setPerformative(ACLMessage.REFUSE);         // ustalenie typu wiadomości (odmowa)
              reply.setContent("tytuł chwilowo niedostępny");                  // treść wiadomości
            }
            myAgent.send(reply);                                // wysłanie odpowiedzi
          }
          else if (msg.getPerformative() == ACLMessage.PROPOSE)
          {
            Integer currentPrice = (Integer) currentPrices.get(senderKey);
            Integer count = (Integer) decrementCounts.get(senderKey);
            System.out.println("Agent-sprzedawca " + getAID().getName() + " otrzymał kontrofertę kupca: " + msg.getContent());

            if (currentPrice == null || count == null)
            {
              reply.setPerformative(ACLMessage.REFUSE);
              reply.setContent("brak aktywnej negocjacji");
              myAgent.send(reply);
              return;
            }
            if (count.intValue() >= MAX_DECREMENTS)
            {
              System.out.println("Agent-sprzedawca " + getAID().getName() + " odrzuca (wyczerpany limit obniżek)");
              reply.setPerformative(ACLMessage.REFUSE);
              reply.setContent("limit obniżek wyczerpany");
              myAgent.send(reply);
              return;
            }
            int newPrice = Math.max(0, currentPrice.intValue() - PRICE_DECREMENT);
            decrementCounts.put(senderKey, new Integer(count.intValue() + 1));
            currentPrices.put(senderKey, new Integer(newPrice));
            reply.setPerformative(ACLMessage.PROPOSE);
            reply.setContent(String.valueOf(newPrice));
            System.out.println("Agent-sprzedawca " + getAID().getName() + " nowa oferta po obniżce: " + newPrice);
            myAgent.send(reply);
          }
        }
        else                       // jeśli wiadomość nie nadeszła, lub była niezgodna z szablonem
        {
          block();                 // blokada metody action() dopóki nie pojawi się nowa wiadomość
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
          System.out.println("Agent-sprzedawca (wersja g lato,2025/2026) "+getAID().getName()+" sprzedał książkę o tytule: "+title);
          String senderKey = msg.getSender().getName();
          currentPrices.remove(senderKey);
          decrementCounts.remove(senderKey);
          myAgent.send(reply);
        }
        else
        {
          block();
        }
      }
    } // Koniec klasy wewnętrznej będącej rozszerzeniem klasy CyclicBehaviour
} // Koniec klasy będącej rozszerzeniem klasy Agent
