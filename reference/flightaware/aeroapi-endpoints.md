# AeroAPI v4.30.0 - Endpoint Reference

Base URL: `https://aeroapi.flightaware.com/aeroapi`
Auth: `x-apikey` header with API key

## Flights
- GET /flights/{id}/map
- GET /flights/{id}/position
- GET /flights/{id}/route
- GET /flights/{id}/track
- GET /flights/{ident}
- GET /flights/{ident}/canonical
- POST /flights/{ident}/intents
- GET /flights/search
- GET /flights/search/advanced
- GET /flights/search/count
- GET /flights/search/positions

## Foresight (Premium)
- GET /foresight/flights/{id}/position
- GET /foresight/flights/{ident}
- GET /foresight/flights/search/advanced

## Airports
- GET /airports
- GET /airports/{id}
- GET /airports/{id}/canonical
- GET /airports/{id}/delays
- GET /airports/{id}/flights
- GET /airports/{id}/flights/arrivals
- GET /airports/{id}/flights/arrivals/cancellations
- GET /airports/{id}/flights/counts
- GET /airports/{id}/flights/departures
- GET /airports/{id}/flights/departures/cancellations
- GET /airports/{id}/flights/scheduled_arrivals
- GET /airports/{id}/flights/scheduled_departures
- GET /airports/{id}/flights/to/{dest_id}
- GET /airports/{id}/nearby
- GET /airports/{id}/routes/{dest_id}
- GET /airports/{id}/weather/forecast
- GET /airports/{id}/weather/observations
- GET /airports/delays
- GET /airports/nearby

## Operators
- GET /operators
- GET /operators/{id}
- GET /operators/{id}/canonical
- GET /operators/{id}/flights
- GET /operators/{id}/flights/arrivals
- GET /operators/{id}/flights/cancellations
- GET /operators/{id}/flights/counts
- GET /operators/{id}/flights/enroute
- GET /operators/{id}/flights/scheduled

## Alerts
- GET /alerts
- POST /alerts
- GET /alerts/{id}
- PUT /alerts/{id}
- DELETE /alerts/{id}
- GET /alerts/endpoint
- PUT /alerts/endpoint
- DELETE /alerts/endpoint

### Alert event types (from backend code)
- arrival
- departure
- cancelled
- diverted
- filed
- eta

### Alert webhook POST payload fields
- long_description
- short_description
- summary
- event_code
- alert_id
- flight.fa_flight_id
- flight.ident
- flight.registration
- flight.aircraft_type
- flight.origin
- flight.destination

### Alert configuration fields
- ident (tail number or flight number)
- origin (airport code, optional)
- destination (airport code, optional)
- aircraft_type (optional)
- start (date, optional)
- end (date, optional)
- max_weekly (default 1000)
- events: {arrival, departure, cancelled, diverted, filed}
- eta (integer)

## History
- GET /history/aircraft/{registration}/last_flight
- GET /history/airports/{id}/flights/arrivals
- GET /history/airports/{id}/flights/arrivals/cancellations
- GET /history/airports/{id}/flights/departures
- GET /history/airports/{id}/flights/departures/cancellations
- GET /history/airports/{id}/flights/to/{dest_id}
- GET /history/flights/{id}/map
- GET /history/flights/{id}/route
- GET /history/flights/{id}/track
- GET /history/flights/{ident}
- GET /history/operators/{id}/flights
- GET /history/operators/{id}/flights/cancellations

## Miscellaneous
- GET /aircraft/{ident}/blocked
- GET /aircraft/{ident}/owner
- GET /aircraft/types/{type}
- GET /disruption_counts/{entity_type}
- GET /disruption_counts/{entity_type}/{id}
- GET /schedules/{date_start}/{date_end}

## Account
- GET /account/usage
